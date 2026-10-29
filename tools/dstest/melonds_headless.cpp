// Headless DS test runner built on the melonDS emulation core.
//   melonds_headless game.nds out_dir step...
// Set SDDIR=<folder> to give the DS a virtual SD card with that folder's files.
// Set GRAM=<hex address of g_ram> (from the .map) to enable "mode:XX[:max]",
// which runs frames until game mode ($7E0100) equals XX.
// Set WAV=<file> to record the DS's audio output (stereo 16-bit, 48 kHz).
// Steps: "N" run N frames, "N:a+right" run N frames holding keys,
//        "shot" save both screens as PPM, "touch:x,y" / "untouch".
// With MAP=<smw-nds.map>: "set:SYMBOL,VALUE[,SIZE]" writes a global, and
// "refshot" saves a SMW_NDS_TEST build's reference picture.
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "NDS.h"
#include "NDSCart.h"
#include "GPU.h"
#include "GPU_Soft.h"
#include "Platform.h"
#include "FATStorage.h"

using namespace melonDS;

namespace melonDS::Platform {
struct FileHandle { FILE *f; };
struct Thread { std::thread t; };
struct Semaphore { std::mutex m; std::condition_variable cv; int count = 0; };
struct Mutex { std::mutex m; };

void SignalStop(StopReason, void *) {}
std::string GetLocalFilePath(const std::string &f) { return f; }
FileHandle *OpenFile(const std::string &path, FileMode mode) {
  const char *m = "rb";
  if ((mode & FileMode::Write) && (mode & FileMode::Read)) m = (mode & FileMode::Preserve) ? "r+b" : "w+b";
  else if (mode & FileMode::Write) m = (mode & FileMode::Append) ? "ab" : "wb";
  FILE *f = fopen(path.c_str(), m);
  if (!f && (mode & FileMode::Write) && (mode & FileMode::Preserve)) f = fopen(path.c_str(), "w+b");
  return f ? new FileHandle{f} : nullptr;
}
FileHandle *OpenLocalFile(const std::string &p, FileMode m) { return OpenFile(p, m); }
bool FileExists(const std::string &n) { FILE *f = fopen(n.c_str(), "rb"); if (f) fclose(f); return f; }
bool LocalFileExists(const std::string &n) { return FileExists(n); }
bool CheckFileWritable(const std::string &) { return true; }
bool CheckLocalFileWritable(const std::string &) { return true; }
bool CloseFile(FileHandle *h) { if (!h) return false; fclose(h->f); delete h; return true; }
bool IsEndOfFile(FileHandle *h) { return feof(h->f); }
bool FileReadLine(char *s, int n, FileHandle *h) { return fgets(s, n, h->f) != nullptr; }
u64 FilePosition(FileHandle *h) { return ftell(h->f); }
bool FileSeek(FileHandle *h, s64 off, FileSeekOrigin o) {
  int w = o == FileSeekOrigin::Start ? SEEK_SET : o == FileSeekOrigin::Current ? SEEK_CUR : SEEK_END;
  return fseek(h->f, off, w) == 0;
}
void FileRewind(FileHandle *h) { rewind(h->f); }
u64 FileRead(void *d, u64 s, u64 c, FileHandle *h) { return fread(d, s, c, h->f); }
bool FileFlush(FileHandle *h) { return fflush(h->f) == 0; }
u64 FileWrite(const void *d, u64 s, u64 c, FileHandle *h) { return fwrite(d, s, c, h->f); }
u64 FileWriteFormatted(FileHandle *h, const char *fmt, ...) {
  va_list a; va_start(a, fmt); int r = vfprintf(h->f, fmt, a); va_end(a); return r;
}
u64 FileLength(FileHandle *h) { long p = ftell(h->f); fseek(h->f, 0, SEEK_END); long l = ftell(h->f); fseek(h->f, p, SEEK_SET); return l; }
void Log(LogLevel level, const char *fmt, ...) {
  if (level == LogLevel::Debug && !getenv("MDEBUG")) return;
  va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a);
}
Thread *Thread_Create(std::function<void()> f) { auto *t = new Thread; t->t = std::thread(f); return t; }
void Thread_Free(Thread *t) { if (t->t.joinable()) t->t.detach(); delete t; }
void Thread_Wait(Thread *t) { if (t->t.joinable()) t->t.join(); }
Semaphore *Semaphore_Create() { return new Semaphore; }
void Semaphore_Free(Semaphore *s) { delete s; }
void Semaphore_Reset(Semaphore *s) { std::lock_guard<std::mutex> l(s->m); s->count = 0; }
void Semaphore_Wait(Semaphore *s) { std::unique_lock<std::mutex> l(s->m); s->cv.wait(l, [&]{ return s->count > 0; }); s->count--; }
bool Semaphore_TryWait(Semaphore *s, int ms) {
  std::unique_lock<std::mutex> l(s->m);
  if (!s->cv.wait_for(l, std::chrono::milliseconds(ms), [&]{ return s->count > 0; })) return false;
  s->count--; return true;
}
void Semaphore_Post(Semaphore *s, int n) { { std::lock_guard<std::mutex> l(s->m); s->count += n; } s->cv.notify_all(); }
Mutex *Mutex_Create() { return new Mutex; }
void Mutex_Free(Mutex *m) { delete m; }
void Mutex_Lock(Mutex *m) { m->m.lock(); }
void Mutex_Unlock(Mutex *m) { m->m.unlock(); }
bool Mutex_TryLock(Mutex *m) { return m->m.try_lock(); }
void Sleep(u64 us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }
u64 GetMSCount() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
u64 GetUSCount() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void WriteNDSSave(const u8 *, u32, u32, u32, void *) {}
void WriteGBASave(const u8 *, u32, u32, u32, void *) {}
void WriteFirmware(const Firmware &, u32, u32, void *) {}
void WriteDateTime(int, int, int, int, int, int, void *) {}
void MP_Begin(void *) {}
void MP_End(void *) {}
int MP_SendPacket(u8 *, int, u64, void *) { return 0; }
int MP_RecvPacket(u8 *, u64 *, void *) { return 0; }
int MP_SendCmd(u8 *, int, u64, void *) { return 0; }
int MP_SendReply(u8 *, int, u64, u16, void *) { return 0; }
int MP_SendAck(u8 *, int, u64, void *) { return 0; }
int MP_RecvHostPacket(u8 *, u64 *, void *) { return 0; }
u16 MP_RecvReplies(u8 *, u64, u16, void *) { return 0; }
int Net_SendPacket(u8 *, int, void *) { return 0; }
int Net_RecvPacket(u8 *, void *) { return 0; }
void Camera_Start(int, void *) {}
void Camera_Stop(int, void *) {}
void Camera_CaptureFrame(int, u32 *, int, int, bool, void *) {}
void Mic_Start(void *) {}
void Mic_Stop(void *) {}
int Mic_ReadInput(s16 *, int, void *) { return 0; }
AACDecoder *AAC_Init() { return nullptr; }
void AAC_DeInit(AACDecoder *) {}
bool AAC_Configure(AACDecoder *, int, int) { return false; }
bool AAC_DecodeFrame(AACDecoder *, const void *, int, void *, int) { return false; }
bool Addon_KeyDown(KeyType, void *) { return false; }
void Addon_RumbleStart(u32, void *) {}
void Addon_RumbleStop(void *) {}
float Addon_MotionQuery(MotionQueryType, void *) { return 0; }
DynamicLibrary *DynamicLibrary_Load(const char *) { return nullptr; }
void DynamicLibrary_Unload(DynamicLibrary *) {}
void *DynamicLibrary_LoadFunction(DynamicLibrary *, const char *) { return nullptr; }
}  // namespace melonDS::Platform

static u32 KeyBits(const std::string &keys) {
  static const char *names[] = {"a", "b", "select", "start", "right", "left", "up", "down", "r", "l", "x", "y"};
  u32 held = 0;
  size_t pos = 0;
  while (pos < keys.size()) {
    size_t e = keys.find('+', pos);
    std::string k = keys.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
    for (int i = 0; i < 12; i++) if (k == names[i]) held |= 1u << i;
    if (e == std::string::npos) break;
    pos = e + 1;
  }
  return held;
}

static void SaveScreens(NDS &nds, const std::string &path) {
  void *top = nullptr, *bottom = nullptr;
  nds.GPU.GetFramebuffers(&top, &bottom);
  FILE *f = fopen(path.c_str(), "wb");
  fprintf(f, "P6\n256 384\n255\n");
  for (void *scr : {top, bottom}) {
    const u32 *p = (const u32 *)scr;
    for (int i = 0; i < 256 * 192; i++) {
      u8 rgb[3] = {(u8)(p[i] >> 16), (u8)(p[i] >> 8), (u8)p[i]};
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
}

// Address of a global from the linker map (env MAP=<file.map>), or 0.
static u32 MapSymbol(const std::string &name) {
  const char *m = getenv("MAP");
  if (!m) return 0;
  std::ifstream f(m);
  std::string line;
  while (std::getline(f, line)) {
    char addr[32], sym[256];
    if (sscanf(line.c_str(), " 0x%31s %255s", addr, sym) == 2 && name == sym &&
        line.find_first_not_of(" \t", line.find(sym) + name.size()) == std::string::npos)
      return strtoul(addr, nullptr, 16);
  }
  fprintf(stderr, "symbol %s not found\n", name.c_str());
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: melonds_headless game.nds out_dir steps...\n"); return 2; }
  std::ifstream in(argv[1], std::ios::binary);
  std::vector<u8> rom((std::istreambuf_iterator<char>(in)), {});
  NDSArgs args;
  args.JIT = std::nullopt;  // accuracy over speed for testing
  auto nds_ptr = std::make_unique<NDS>(std::move(args));  // too big for the stack
  NDS &nds = *nds_ptr;
  nds.SetRenderer(std::make_unique<SoftRenderer>(nds));
  std::optional<NDSCart::NDSCartArgs> cart_args;
  if (const char *sd = getenv("SDDIR")) {
    std::string img = std::string(argv[2]) + "/sd.img";
    remove(img.c_str());
    cart_args = NDSCart::NDSCartArgs{FATStorageArgs{img, 64ull << 20, false, std::string(sd)}};
  }
  auto cart = NDSCart::ParseROM(rom.data(), (u32)rom.size(), nullptr, std::move(cart_args));
  if (!cart) { fprintf(stderr, "bad rom\n"); return 1; }
  nds.SetNDSCart(std::move(cart));
  nds.Reset();
  nds.SetupDirectBoot("game.nds");
  nds.Start();
  int frame = 0, shot = 0;
  FILE *wav = nullptr;
  uint32_t wav_samples = 0;
  if (const char *w = getenv("WAV")) {
    wav = fopen(w, "wb");
    fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x80\xbb\0\0\0\xee\x02\0\x04\0\x10\0data\0\0\0\0", 1, 44, wav);
  }
  // pin:ADDR,VAL keeps game RAM byte ADDR (offset into g_ram, needs GRAM)
  // at VAL before every frame; unpin:ADDR stops.
  std::vector<std::pair<u32, u8>> pins;
  const u32 gram = getenv("GRAM") ? strtoul(getenv("GRAM"), nullptr, 16) : 0;
  auto apply_pins = [&]() { for (auto &p : pins) nds.ARM9Write8(gram + p.first, p.second); };
  auto drain_audio = [&]() {
    s16 buf[2048 * 2];
    int n;
    while ((n = nds.SPU.ReadOutput(buf, 2048)) > 0) {
      if (wav) { fwrite(buf, 4, n, wav); wav_samples += n; }
    }
  };
  for (int i = 3; i < argc; i++) {
    std::string s = argv[i];
    if (s == "shot") {
      char p[512];
      snprintf(p, sizeof p, "%s/ds%02d_f%05d.ppm", argv[2], shot++, frame);
      SaveScreens(nds, p);
      continue;
    }
    if (s.rfind("set:", 0) == 0) {  // set:SYMBOL,VALUE[,SIZE]  write a global (needs MAP)
      char sym[256] = "";
      unsigned v = 0, size = 1;
      sscanf(s.c_str() + 4, "%255[^,],%i,%u", sym, (int *)&v, &size);
      const u32 a = MapSymbol(sym);
      if (a && size == 1) nds.ARM9Write8(a, (u8)v);
      if (a && size == 2) nds.ARM9Write16(a, (u16)v);
      if (a && size == 4) nds.ARM9Write32(a, v);
      continue;
    }
    if (s == "refshot") {  // the test build's reference picture (g_test_ref) as PPM
      static u32 ref = MapSymbol("g_test_ref"), crop = MapSymbol("g_test_ref_crop");
      char p[512];
      snprintf(p, sizeof p, "%s/ref%02d_f%05d_c%02d.ppm", argv[2], shot, frame, crop ? nds.ARM9Read8(crop) : 0);
      FILE *f = fopen(p, "wb");
      fprintf(f, "P6\n256 224\n255\n");
      for (int i = 0; i < 256 * 224; i++) {
        const u32 px = nds.ARM9Read32(ref + i * 4);
        const u8 rgb[3] = {(u8)(px >> 16), (u8)(px >> 8), (u8)px};
        fwrite(rgb, 1, 3, f);
      }
      fclose(f);
      continue;
    }
    if (s.rfind("touch:", 0) == 0) { int x, y; sscanf(s.c_str() + 6, "%d,%d", &x, &y); nds.TouchScreen(x, y); continue; }
    if (s == "untouch") { nds.ReleaseScreen(); continue; }
    if (s.rfind("pin:", 0) == 0) {
      unsigned a = 0, v = 0;
      sscanf(s.c_str() + 4, "%x,%x", &a, &v);
      pins.push_back({a, (u8)v});
      continue;
    }
    if (s.rfind("unpin:", 0) == 0) {
      unsigned a = 0;
      sscanf(s.c_str() + 6, "%x", &a);
      for (size_t k = 0; k < pins.size(); k++) if (pins[k].first == a) pins.erase(pins.begin() + k--);
      continue;
    }
    if (s.rfind("until:", 0) == 0) {  // until:ADDR,VAL[,keys]  (max 20000 frames)
      unsigned a = 0, v = 0;
      char keys[64] = "";
      sscanf(s.c_str() + 6, "%x,%x,%63s", &a, &v, keys);
      nds.SetKeyMask(0xFFF & ~KeyBits(keys));
      unsigned k = 0;
      while (gram && k < 20000 && nds.ARM9Read8(gram + a) != v) { apply_pins(); nds.RunFrame(); drain_audio(); frame++; k++; }
      printf("until %x==%x: frame %d%s\n", a, v, frame, k >= 20000 ? " TIMEOUT" : "");
      continue;
    }
    if (s.rfind("mem:", 0) == 0) {  // mem:ADDR[,ADDR...] absolute 32-bit reads
      printf("frame %d:", frame);
      size_t pos = 4;
      while (pos < s.size()) {
        size_t e = s.find(',', pos);
        u32 a = strtoul(s.substr(pos, e == std::string::npos ? std::string::npos : e - pos).c_str(), nullptr, 16);
        printf(" [%08x]=%u", a, nds.ARM9Read32(a));
        if (e == std::string::npos) break;
        pos = e + 1;
      }
      printf("\n");
      continue;
    }
    if (s.rfind("ram:", 0) == 0) {  // ram:ADDR,ADDR,... print game RAM bytes
      printf("frame %d:", frame);
      size_t pos = 4;
      while (pos < s.size()) {
        size_t e = s.find(',', pos);
        u32 a = strtoul(s.substr(pos, e == std::string::npos ? std::string::npos : e - pos).c_str(), nullptr, 16);
        printf(" [%x]=%02x", a, nds.ARM9Read8(gram + a));
        if (e == std::string::npos) break;
        pos = e + 1;
      }
      printf("\n");
      continue;
    }
    if (s.rfind("mode:", 0) == 0) {
      const char *g = getenv("GRAM");
      unsigned want = 0, max = 3000;
      sscanf(s.c_str() + 5, "%x:%u", &want, &max);
      u32 gram = g ? strtoul(g, nullptr, 16) : 0;
      unsigned k = 0;
      nds.SetKeyMask(0xFFF);
      while (gram && k < max && nds.ARM9Read8(gram + 0x100) != want) { nds.RunFrame(); drain_audio(); frame++; k++; }
      if (k >= max) printf("mode %02x not reached by frame %d\n", want, frame);
      continue;
    }
    if (s.rfind("poke:", 0) == 0) {  // poke:addr,value (8-bit write to ARM9 memory)
      u32 a = 0, v = 0;
      sscanf(s.c_str() + 5, "%x,%x", &a, &v);
      nds.ARM9Write8(a, (u8)v);
      continue;
    }
    if (s.rfind("io:", 0) == 0) {  // io:0x04000000,0x04000048 -> print 16-bit I/O registers
      printf("frame %d:", frame);
      size_t pos = 3;
      while (pos < s.size()) {
        size_t e = s.find(',', pos);
        u32 a = strtoul(s.substr(pos, e == std::string::npos ? std::string::npos : e - pos).c_str(), nullptr, 16);
        printf(" [%08x]=%04x", a, nds.ARM9IORead16(a));
        if (e == std::string::npos) break;
        pos = e + 1;
      }
      printf("\n");
      continue;
    }
    size_t c = s.find(':');
    int n = atoi(s.substr(0, c).c_str());
    u32 held = c == std::string::npos ? 0 : KeyBits(s.substr(c + 1));
    nds.SetKeyMask(0xFFF & ~held);
    for (int k = 0; k < n; k++, frame++) { apply_pins(); nds.RunFrame(); drain_audio(); }
  }
  if (wav) {
    uint32_t data = wav_samples * 4, riff = data + 36;
    fseek(wav, 4, SEEK_SET); fwrite(&riff, 4, 1, wav);
    fseek(wav, 40, SEEK_SET); fwrite(&data, 4, 1, wav);
    fclose(wav);
  }
  printf("ran %d frames\n", frame);
  return 0;
}
