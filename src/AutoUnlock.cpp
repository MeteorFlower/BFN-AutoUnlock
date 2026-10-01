// ============================================================================
//  AutoUnlock.dll，一键全量解锁（PvZ: Battle for Neighborville）
// ----------------------------------------------------------------------------
//  原理：游戏内置了调试消息 Blaze::PvzGw::DebugGrantItemsRequest。本 DLL 自己拼装
//  该请求（Entry 对象 + TdfPrimitiveMap 容器 + 请求对象），投递给游戏的 Blaze 消息
//  管线，由游戏自身完成发放，所以物品真正进档案，不是内存假象。用到的全是游戏 exe
//  内的函数/数据地址，不依赖任何第三方 DLL。
//
//  本 DLL 不打内存补丁：让连接得以建立的那几条由游戏目录下的 RtWorkQ.dll
//  （AutoOffline）在进程启动时完成。缺了它连地图都进不去，发奖会静默失败。
//
//  构建：build.bat（MSVC）
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

// --------------------------------------------------------------------------
// 配置
// --------------------------------------------------------------------------
#define GRANT_DELAY_MS 10  // 兜底间隔，items.txt 里的 @delay 会覆盖它

static FILE *g_log = nullptr;
static std::string g_dir;
static int g_delay = GRANT_DELAY_MS;
static HANDLE g_con = nullptr;
static volatile LONG g_abort = 0;  // Ctrl+C 置位，发放循环每条检查一次

// --------------------------------------------------------------------------
// 控制台与日志
// --------------------------------------------------------------------------
static void ConInit() {
  if (!GetConsoleWindow())
    AllocConsole();
  g_con = GetStdHandle(STD_OUTPUT_HANDLE);
  if (g_con == INVALID_HANDLE_VALUE)
    g_con = nullptr;
}

static void ConWrite(const char *s) {
  if (!g_con)
    return;
  DWORD n = 0;
  if (!WriteConsoleA(g_con, s, (DWORD)strlen(s), &n, nullptr))
    WriteFile(g_con, s, (DWORD)strlen(s), &n, nullptr);
}

// Ctrl+C 只置一个标志，收尾交给发放循环自己。必须返回 TRUE：这个进程就是游戏本体，
// 交给默认处理会直接把游戏结束掉。
static BOOL WINAPI CtrlHandler(DWORD type) {
  if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
    InterlockedExchange(&g_abort, 1);
    return TRUE;
  }
  return FALSE;
}

// 自己收按键，不用 ReadConsoleA：阻塞读在 Ctrl+C 到来时不会返回，那样就卡死了。
// 行输入与回显关掉（由这里自己实现），ENABLE_PROCESSED_INPUT 保留，
// 这样 Ctrl+C 仍然走控制事件而不是落进输入缓冲。
// 返回 false = 被 Ctrl+C 打断，调用方应当退出。
static bool ReadConsoleLine(char *out, int cap) {
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  if (!hIn || hIn == INVALID_HANDLE_VALUE)
    return false;

  DWORD mode = 0;
  if (GetConsoleMode(hIn, &mode))
    SetConsoleMode(hIn, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
  FlushConsoleInputBuffer(hIn);

  int n = 0;
  for (;;) {
    if (g_abort)
      return false;

    DWORD avail = 0;
    if (!GetNumberOfConsoleInputEvents(hIn, &avail))
      return false;  // 不是控制台句柄（stdin 被重定向），无法交互
    if (avail == 0) {
      Sleep(50);
      continue;
    }

    INPUT_RECORD rec;
    DWORD got = 0;
    if (!ReadConsoleInputA(hIn, &rec, 1, &got) || got == 0)
      continue;
    if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown)
      continue;

    const char c = rec.Event.KeyEvent.uChar.AsciiChar;
    if (c == '\r') {
      out[n] = 0;
      ConWrite("\r\n");
      return true;
    }
    if (c == 8) {  // 退格
      if (n > 0) {
        --n;
        ConWrite("\b \b");
      }
      continue;
    }
    if (c >= 32 && c < 127 && n < cap - 1) {
      out[n++] = c;
      const char echo[2] = {c, 0};
      ConWrite(echo);
    }
  }
}

static void Log(const char *fmt, ...) {
  char body[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);

  SYSTEMTIME st;
  GetLocalTime(&st);
  char line[1200];
  _snprintf_s(line, sizeof(line), _TRUNCATE, "[%02d:%02d:%02d] %s", st.wHour, st.wMinute,
              st.wSecond, body);

  ConWrite(line);
  ConWrite("\r\n");
  if (g_log) {
    fputs(line, g_log);
    fputc('\n', g_log);
    fflush(g_log);
  }
}

// --------------------------------------------------------------------------
// 游戏地址（全部来自 PvZBattleforNeighborville.exe，与本机版本已实证匹配）
// --------------------------------------------------------------------------
#define G_ALLOC0 0x1424B79F0ull     // 分配器管理器
#define G_SETSTRING 0x142664480ull  // 字符串赋值
#define G_INIT192 0x1424B1DB0ull    // 初始化 192 字节请求对象
#define G_DISPATCH 0x14252ECF0ull   // 投递到 Blaze 消息管线
#define G_MANAGER 0x14464E560ull    // 全局管理器指针

#define VT_ENTRY 0x143727040ull  // DebugGrantItemsRequest::Entry
#define VT_OUTER 0x143727180ull
#define VT_MAP 0x1437270F0ull  // TdfPrimitiveMap<Entry*,1>
#define VT_REQUEST 0x14377BCE8ull
#define SENTINEL 0x14481E593ull  // "空字符串"哨兵

// 不要另建 GrantOffersRequest 路径（消息号 39、外层 vtable 0x143725420、元素是 24
// 字节内联 TdfString）：实测走它发 2654 条毫无反应。而 DebugGrantItemsRequest 直接
// 吃 offer 别名，把 "SuperHero_HeadProp_IroningMan_VisualAsset_offer" 当普通物品名发，
// Super Brainz 的 "Ironing Out" 立刻到账。所以所有解锁名（含 _offer）统一走 items.txt。
#define MSG_ID_ITEMS 34

// 对应原 DLL 的 _guard_check_icall_nop（就是一条 ret）
static void __fastcall GuardNop(void *) {}

typedef void *(__fastcall *fn_alloc0)(long long);
typedef void *(__fastcall *fn_vtbl_alloc)(void *self, long long size, long long a, long long b);
typedef void(__fastcall *fn_setstr)(void *dst, const char *src, long long);
typedef void(__fastcall *fn_init192)(void *obj, unsigned short a, long long b, long long c,
                                     void *d);
typedef void(__fastcall *fn_dispatch)(void *, void *, unsigned short, long long, void *, void *,
                                      void *, long long);

// --------------------------------------------------------------------------
//  发放单条：自行构造 DebugGrantItemsRequest 并投递
// --------------------------------------------------------------------------
// 返回 false = 我们自己的指针链是空的（不是游戏侧出错）。这类失败会单独打一行说明，
// 所以日志里出现 failed 却没有任何原因行，那才是真的被 SEH 抓到的崩溃。
static bool GrantItem(const char *name) {
  if (!name || !*name) {
    Log("!! empty unlock id, nothing sent");
    return false;
  }

  // ---- 1. 分配器 ----
  void *mgr = ((fn_alloc0)G_ALLOC0)(0);
  if (!mgr) {
    Log("!! G_ALLOC0 returned null, the address table may not match this build");
    return false;
  }
  void **vt = *(void ***)mgr;
  if (!vt) {
    Log("!! allocator at %p has a null vtable", mgr);
    return false;
  }

  // ---- 2. 外层容器（0x60 字节，Tdf + TdfPrimitiveMap） ----
  __declspec(align(16)) unsigned char S[0x60];
  memset(S, 0, sizeof(S));
  *(void **)(S + 0x00) = (void *)VT_OUTER;  // 外层 vtable
  *(unsigned *)(S + 0x08) = 0x80000000u;
  *(void **)(S + 0x20) = (void *)VT_MAP;  // TdfPrimitiveMap vtable
  *(unsigned *)(S + 0x28) = 0x80000000u;
  *(void **)(S + 0x50) = mgr;  // 分配器

  // ---- 3. Entry 对象（72 字节） ----
  void *e = ((fn_vtbl_alloc)vt[2])(mgr, 72, 0, 1);
  memset(e, 0, 72);
  *(unsigned *)((char *)e + 0x08) = 0x80000000u;
  *(void **)((char *)e + 0x10) = (void *)SENTINEL;
  *(void **)((char *)e + 0x18) = mgr;
  *(void **)((char *)e + 0x30) = (void *)SENTINEL;
  *(void **)((char *)e + 0x38) = mgr;
  *(void **)e = (void *)VT_ENTRY;
  ((fn_setstr)G_SETSTRING)((char *)e + 0x10, name, 0);  // 写入解锁 ID
  *(unsigned *)((char *)e + 0x28) = 1;                  // Active = 1

  // ---- 4. 把 Entry 放进 map 的 vector（单元素） ----
  void *arr = ((fn_vtbl_alloc)vt[2])(mgr, 8, 0, 1);
  *(void **)arr = e;
  *(void **)(S + 0x38) = arr;              // begin
  *(void **)(S + 0x40) = (char *)arr + 8;  // end
  *(void **)(S + 0x48) = (char *)arr + 8;  // cap
  *(unsigned char *)(S + 0x30) |= 1;       // 标志位

  // ---- 5. 建请求对象(192 字节) ----
  void *g = *(void **)G_MANAGER;
  void *m2 = g ? *(void **)((char *)g + 0x68) : nullptr;
  void *r = m2 ? *(void **)((char *)m2 + 8) : nullptr;
  if (!r) {
    Log("!! dispatch context is null (G_MANAGER chain: g=%p m2=%p)", g, m2);
    return false;
  }
  unsigned short tag = *(unsigned short *)((char *)m2 + 0x10);

  __declspec(align(16)) unsigned char B0[0x10];
  memset(B0, 0, sizeof(B0));
  long long v29[3] = {0, 1, 0};

  void *mgr2 = ((fn_alloc0)G_ALLOC0)(129);
  void **vt2 = *(void ***)mgr2;
  void *req = ((fn_vtbl_alloc)vt2[2])(mgr2, 192, 0, 1);
  ((fn_init192)G_INIT192)(req, tag, 34, 0, r);
  *(void **)req = (void *)VT_REQUEST;
  *(void **)((char *)req + 0xA0) = (void *)&GuardNop;
  *(void **)((char *)req + 0xB0) = (void *)&GuardNop;

  // ---- 6. 投递 ----
  ((fn_dispatch)G_DISPATCH)(r, B0, tag, MSG_ID_ITEMS, S, req, v29, 0);
  // 故意不调析构：每次泄漏约 272 字节，发 3500 条约 1MB，可忽略。省去析构也少一处出错点。
  return true;
}

// --------------------------------------------------------------------------
// 工具
// --------------------------------------------------------------------------
// 用比 MAX_PATH 大的缓冲：GetModuleFileNameA 在路径过长时截断而不报错，
// 截断后算出来的目录会指向别处，items.txt 就找不到了。
static std::string DirOfSelf(HMODULE self) {
  char p[1024] = {0};
  const DWORD n = GetModuleFileNameA(self, p, sizeof(p));
  if (n == 0 || n >= sizeof(p)) {
    Log("!! GetModuleFileNameA failed (err=%lu), using the current directory", GetLastError());
    return ".";
  }
  std::string s(p);
  size_t k = s.find_last_of("\\/");
  return (k == std::string::npos) ? std::string(".") : s.substr(0, k);
}

static std::string Trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
    return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

struct Entry {
  std::string id;
  int repeat;
};

// 一行一个 ID；# 或 // 注释；@delay <ms> 调间隔；xN <ID> 重复 N 次
static std::vector<Entry> LoadItems(const std::string &path) {
  std::vector<Entry> out;
  FILE *f = fopen(path.c_str(), "r");
  if (!f) {
    Log("!! Cannot open list file: %s", path.c_str());
    return out;
  }

  char buf[512];
  while (fgets(buf, sizeof(buf), f)) {
    // 超长行会被 fgets 静默劈成两段，后半截会被当成一个合法 ID 发出去。
    // 末行没有换行符是正常的，所以只在还没到 EOF 时才判定为超长。
    if (!strchr(buf, '\n') && !feof(f)) {
      Log("!! line longer than %d bytes, skipping it", (int)sizeof(buf) - 1);
      int c;
      while ((c = fgetc(f)) != EOF && c != '\n')
        ;
      continue;
    }
    std::string s = Trim(buf);
    if (s.empty() || s[0] == '#' || s.rfind("//", 0) == 0)
      continue;

    if (s[0] == '@') {
      if (s.rfind("@delay", 0) == 0) {
        // 后面必须真的跟一个数字：@delay=10 这种手滑会被 atoi 解析成 0，间隔变成 Sleep(0)。
        const std::string arg = Trim(s.substr(6));
        if (arg.empty() || !isdigit((unsigned char)arg[0])) {
          Log("!! @delay needs a number, keeping %d ms: %s", g_delay, s.c_str());
        } else {
          int v = atoi(arg.c_str());
          if (v >= 0 && v <= 5000)
            g_delay = v;
          else
            Log("!! @delay out of 0..5000, keeping %d ms: %s", g_delay, s.c_str());
        }
      }
      continue;
    }

    int rep = 1;
    if ((s[0] == 'x' || s[0] == 'X') && s.size() > 1) {
      size_t i = 1;
      while (i < s.size() && isdigit((unsigned char)s[i]))
        ++i;
      if (i > 1) {
        rep = atoi(s.substr(1, i - 1).c_str());
        s = Trim(s.substr(i));
        if (rep < 1)
          rep = 1;
        if (rep > 100000)
          rep = 100000;
      }
    }
    if (!s.empty())
      out.push_back({s, rep});
  }
  fclose(f);
  return out;
}

// SEH 必须放在没有 C++ 析构对象的函数里（MSVC C2712）
static bool TryGrant(const char *name) {
  __try {
    return GrantItem(name);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool FileExists(const std::string &p) {
  DWORD a = GetFileAttributesA(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// 跑一份名单：统一走 DebugGrantItemsRequest（offer 别名也是从这里发）。
// startIdx 是 0 起的条目下标。返回值 0 = 整份跑完；非 0 = 被 Ctrl+C 打断，
// 值就是下次应当接着跑的那一条（1 起编号，直接喂回输入框即可）。
static size_t RunList(const char *label, const std::vector<Entry> &list, int delay,
                      size_t startIdx) {
  size_t grants = 0;
  for (size_t k = startIdx; k < list.size(); ++k)
    grants += list[k].repeat;
  Log("---- %s: items #%zu..#%zu, %zu grants, %d ms apart ----", label, startIdx + 1, list.size(),
      grants, delay);

  size_t sent = 0, ok = 0, failed = 0;
  for (size_t k = startIdx; k < list.size(); ++k) {
    for (int r = 0; r < list[k].repeat; ++r) {
      if (g_abort) {
        Log("!! interrupted before item #%zu.", k + 1);
        Log("---- %s INTERRUPTED: ok %zu / failed %zu / sent %zu ----", label, ok, failed, sent);
        return k + 1;
      }

      if (TryGrant(list[k].id.c_str()))
        ++ok;
      else {
        // 被 SEH 抓到崩溃时不会有任何原因行，所以"只有这一行"就是崩溃的特征。
        Log("  !! failed: %s (skipping, continuing)", list[k].id.c_str());
        ++failed;
      }
      ++sent;
      // 分片睡：Sleep 不会被我们的 Ctrl+C 处理器打断，切成 100ms 才响应得及时。
      for (int left = delay; left > 0 && !g_abort; left -= 100)
        Sleep((DWORD)((left < 100) ? left : 100));
    }
    // 按条目号报（中断后要接着输入的就是这个号）。判断也用 k+1，否则打出来是
    // 901、951 这种非整数；只在 50 的倍数上报。
    if ((k + 1) % 50 == 0 || k + 1 == list.size())
      Log("  [item %zu/%zu] %s  (%zu grants sent)", k + 1, list.size(), list[k].id.c_str(), sent);
  }
  Log("---- %s DONE: ok %zu / failed %zu / total %zu ----", label, ok, failed, sent);
  return 0;
}

// --------------------------------------------------------------------------
// 主循环
//
// 不检测掉线：被服务器踢掉的样子太多了（弹回标题页 / 卡住 / 直接崩），没有可靠的判据。
// 所以节奏交给玩家：跑一段、想停就 Ctrl+C、回来接着输上一次中断的条目号。
// 没刷完是玩家自己的事，程序不猜。
//
// 这个循环永远不退出：控制台窗口属于游戏进程，工作线程就算结束也关不掉它，
// 只会留下一个死窗口，而玩家还不敢去关（关了游戏就没了）。所以 Ctrl+C 和跑完
// 都回到输入框，随时可以再来一轮。
// --------------------------------------------------------------------------
static DWORD WINAPI Worker(LPVOID self) {
  // ConInit 必须在 DirOfSelf 之前：那时 g_log 还没开，日志只能靠控制台。
  ConInit();
  g_dir = DirOfSelf((HMODULE)self);

  const std::string logPath = g_dir + "\\unlock_log.txt";
  g_log = fopen(logPath.c_str(), "w");  // 用 "w" 打开：每次运行清空，否则日志会无限增长
  if (!g_log)
    Log("!! cannot open %s (errno=%d), the log will only go to this console", logPath.c_str(),
        errno);

  Log("========================================================");
  Log("AutoUnlock injected. Dir: %s", g_dir.c_str());

  if (!SetConsoleCtrlHandler(CtrlHandler, TRUE))
    Log("!! SetConsoleCtrlHandler failed (err=%lu). Do NOT press Ctrl+C: it would kill the game.",
        GetLastError());

  const std::string itemPath = g_dir + "\\items.txt";
  if (!FileExists(itemPath)) {
    Log("!! items.txt not found next to the DLL: %s", itemPath.c_str());
    return 0;
  }
  const std::vector<Entry> items = LoadItems(itemPath);
  if (items.empty()) {
    Log("!! items.txt has no usable entry.");
    return 0;
  }

  Log("Loaded %zu items.", items.size());

  Log("Enter a map first (Giddy Park or any area).");
  Log("Do not close this window, it kills the game.");

  for (;;) {
    InterlockedExchange(&g_abort, 0);  // 上一轮的 Ctrl+C 已经处理完，清掉再问下一轮
    Log("");
    Log("Start from item # (1-%zu, Enter = 1, Ctrl+C to stop):", items.size());
    ConWrite("> ");

    char line[64];
    if (!ReadConsoleLine(line, sizeof(line))) {
      Log("Ctrl+C. Nothing is running.");
      continue;
    }

    const std::string s = Trim(line);
    size_t start = 1;
    if (!s.empty()) {
      const long v = atol(s.c_str());
      if (v < 1 || (size_t)v > items.size()) {
        Log("!! %ld is outside 1-%zu, try again.", v, items.size());
        continue;
      }
      start = (size_t)v;
    }

    Log("================ GRANT START ================");
    const size_t resume = RunList("ITEMS", items, g_delay, start - 1);
    if (resume) {
      Log("================ INTERRUPTED at item #%zu ================", resume);
      Log("Enter %zu to continue from there.", resume);
    } else {
      // 货币数值要重开反作弊才会刷新，这句属于 mod 说明，不在这里重复
      Log("================ ALL DONE ================");
    }
  }
}

// 不要调 DisableThreadLibraryCalls：本 DLL 用 /MT 静态 CRT，CRT 的按线程初始化走
// DLL_THREAD_ATTACH，砍掉线程通知换来的收益极小、风险不小。
// 也不要用 FreeLibrary 卸载本 DLL：请求对象里的 GuardNop 指向本模块，游戏会长期持有。
BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    HANDLE h = CreateThread(nullptr, 0, Worker, self, 0, nullptr);
    if (h)
      CloseHandle(h);
  }
  return TRUE;
}
