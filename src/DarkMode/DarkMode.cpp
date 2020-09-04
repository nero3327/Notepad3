
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#ifdef D_NP3_WIN10_DARK_MODE
#include <CommCtrl.h>
#include <Uxtheme.h>
#include <WindowsX.h>
#include <Dwmapi.h>
#include <Vssym32.h>
#include <climits>
#endif

#include <cstdint>

#include "DarkMode.h"
#include "IatHook.hpp"
#include "ListViewUtil.hpp"

// ============================================================================

using fnRtlGetNtVersionNumbers = void(WINAPI *)(LPDWORD major, LPDWORD minor, LPDWORD build);

extern "C" DWORD GetWindowsBuildNumber(LPDWORD major, LPDWORD minor)
{
  static DWORD _dwWindowsBuildNumber = 0;
  static DWORD _major, _minor;
  if (!_dwWindowsBuildNumber) {
    fnRtlGetNtVersionNumbers RtlGetNtVersionNumbers = reinterpret_cast<fnRtlGetNtVersionNumbers>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetNtVersionNumbers"));
    if (RtlGetNtVersionNumbers) {
      RtlGetNtVersionNumbers(&_major, &_minor, &_dwWindowsBuildNumber);
      _dwWindowsBuildNumber &= ~0xF0000000;
    }
  }
  if (major) { *major = _major; }
  if (minor) { *minor = _minor; }
  return _dwWindowsBuildNumber;
}
// ============================================================================


#ifdef D_NP3_WIN10_DARK_MODE

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Uxtheme.lib")
#pragma comment(lib, "Dwmapi.lib")

enum IMMERSIVE_HC_CACHE_MODE
{
  IHCM_USE_CACHED_VALUE,
  IHCM_REFRESH
};
typedef enum IMMERSIVE_HC_CACHE_MODE IMMERSIVE_HC_CACHE_MODE;

// Insider 18334
enum PreferredAppMode
{
  Default,
  AllowDark,
  ForceDark,
  ForceLight,
  Max
};
typedef enum PreferredAppMode PreferredAppMode;


enum WINDOWCOMPOSITIONATTRIB {
  WCA_UNDEFINED = 0,
  WCA_NCRENDERING_ENABLED = 1,
  WCA_NCRENDERING_POLICY = 2,
  WCA_TRANSITIONS_FORCEDISABLED = 3,
  WCA_ALLOW_NCPAINT = 4,
  WCA_CAPTION_BUTTON_BOUNDS = 5,
  WCA_NONCLIENT_RTL_LAYOUT = 6,
  WCA_FORCE_ICONIC_REPRESENTATION = 7,
  WCA_EXTENDED_FRAME_BOUNDS = 8,
  WCA_HAS_ICONIC_BITMAP = 9,
  WCA_THEME_ATTRIBUTES = 10,
  WCA_NCRENDERING_EXILED = 11,
  WCA_NCADORNMENTINFO = 12,
  WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
  WCA_VIDEO_OVERLAY_ACTIVE = 14,
  WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
  WCA_DISALLOW_PEEK = 16,
  WCA_CLOAK = 17,
  WCA_CLOAKED = 18,
  WCA_ACCENT_POLICY = 19,
  WCA_FREEZE_REPRESENTATION = 20,
  WCA_EVER_UNCLOAKED = 21,
  WCA_VISUAL_OWNER = 22,
  WCA_HOLOGRAPHIC = 23,
  WCA_EXCLUDED_FROM_DDA = 24,
  WCA_PASSIVEUPDATEMODE = 25,
  WCA_USEDARKMODECOLORS = 26,
  WCA_LAST = 27
};


struct WINDOWCOMPOSITIONATTRIBDATA {
  WINDOWCOMPOSITIONATTRIB Attrib;
  PVOID pvData;
  SIZE_T cbData;
};

using fnSetWindowCompositionAttribute = BOOL(WINAPI *)(HWND hWnd, WINDOWCOMPOSITIONATTRIBDATA *);
// 1809 17763
using fnShouldAppsUseDarkMode = bool(WINAPI *)();                                            // ordinal 132
using fnAllowDarkModeForWindow = bool(WINAPI *)(HWND hWnd, bool allow);                      // ordinal 133
using fnAllowDarkModeForApp = bool(WINAPI *)(bool allow);                                    // ordinal 135, in 1809
using fnFlushMenuThemes = void(WINAPI *)();                                                  // ordinal 136
using fnRefreshImmersiveColorPolicyState = void(WINAPI *)();                                 // ordinal 104
using fnIsDarkModeAllowedForWindow = bool(WINAPI *)(HWND hWnd);                              // ordinal 137
using fnGetIsImmersiveColorUsingHighContrast = bool(WINAPI *)(IMMERSIVE_HC_CACHE_MODE mode); // ordinal 106
using fnOpenNcThemeData = HTHEME(WINAPI *)(HWND hWnd, LPCWSTR pszClassList);                 // ordinal 49
// 1903 18362
using fnShouldSystemUseDarkMode = bool(WINAPI *)();                                 // ordinal 138
using fnSetPreferredAppMode = PreferredAppMode(WINAPI *)(PreferredAppMode appMode); // ordinal 135, in 1903
using fnIsDarkModeAllowedForApp = bool(WINAPI *)();                                 // ordinal 139

fnSetWindowCompositionAttribute _SetWindowCompositionAttribute = nullptr;
fnShouldAppsUseDarkMode _ShouldAppsUseDarkMode = nullptr;
fnAllowDarkModeForWindow _AllowDarkModeForWindow = nullptr;
fnAllowDarkModeForApp _AllowDarkModeForApp = nullptr;
fnFlushMenuThemes _FlushMenuThemes = nullptr;
fnRefreshImmersiveColorPolicyState _RefreshImmersiveColorPolicyState = nullptr;
fnIsDarkModeAllowedForWindow _IsDarkModeAllowedForWindow = nullptr;
fnGetIsImmersiveColorUsingHighContrast _GetIsImmersiveColorUsingHighContrast = nullptr;
fnOpenNcThemeData _OpenNcThemeData = nullptr;
// 1903 18362
fnShouldSystemUseDarkMode _ShouldSystemUseDarkMode = nullptr;
fnSetPreferredAppMode _SetPreferredAppMode = nullptr;

// ============================================================================


static bool s_bDarkModeSupported = false;

extern "C" bool IsDarkModeSupported() {
  return s_bDarkModeSupported;
}
// ============================================================================


static bool s_bDarkModeEnabled = false;

extern "C" bool CheckDarkModeEnabled() {
  s_bDarkModeEnabled = _ShouldAppsUseDarkMode() && !IsHighContrast();
  return s_bDarkModeEnabled;
}
// ============================================================================


extern "C" bool AllowDarkModeForWindow(HWND hWnd, bool allow)
{
  return s_bDarkModeSupported ? _AllowDarkModeForWindow(hWnd, allow) : false;
}
// ============================================================================


extern "C" bool IsHighContrast()
{
  HIGHCONTRASTW highContrast = { sizeof(highContrast) };
  return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, FALSE) ? 
    (highContrast.dwFlags & HCF_HIGHCONTRASTON) : false;
}
// ============================================================================


extern "C" void RefreshTitleBarThemeColor(HWND hWnd)
{
  BOOL dark = FALSE;
  if (_IsDarkModeAllowedForWindow(hWnd) &&
      _ShouldAppsUseDarkMode() &&
      !IsHighContrast()) {
    dark = TRUE;
  }
  DWORD const buildNum = GetWindowsBuildNumber(nullptr, nullptr);
  if (buildNum < 18362) {
    SetPropW(hWnd, L"UseImmersiveDarkModeColors", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(dark)));
  }
  else if (_SetWindowCompositionAttribute) {
    WINDOWCOMPOSITIONATTRIBDATA data = { WCA_USEDARKMODECOLORS, &dark, sizeof(dark) };
    _SetWindowCompositionAttribute(hWnd, &data);
  }
}
// ============================================================================


extern "C" bool IsColorSchemeChangeMessage(LPARAM lParam)
{
  bool is = false;
  if (lParam && CompareStringOrdinal(reinterpret_cast<LPCWCH>(lParam), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
  {
    _RefreshImmersiveColorPolicyState();
    is = true;
  }
  _GetIsImmersiveColorUsingHighContrast(IHCM_REFRESH);
  return is;
}
// ============================================================================


extern "C" bool IsColorSchemeChangeMessageEx(UINT message, LPARAM lParam)
{
  return (message == WM_SETTINGCHANGE) ? IsColorSchemeChangeMessage(lParam) : false;
}
// ============================================================================


extern "C" void AllowDarkModeForApp(bool allow)
{
  if (_AllowDarkModeForApp) {
    _AllowDarkModeForApp(allow);
  }
  else if (_SetPreferredAppMode) {
    _SetPreferredAppMode(allow ? AllowDark : Default);
  }
}
// ============================================================================


static void _FixDarkScrollBar()
{
  HMODULE hComctl = LoadLibraryExW(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (hComctl) {
    auto addr = FindDelayLoadThunkInModule(hComctl, "uxtheme.dll", 49); // OpenNcThemeData
    if (addr) {
      DWORD oldProtect;
      if (VirtualProtect(addr, sizeof(IMAGE_THUNK_DATA), PAGE_READWRITE, &oldProtect)) {
        auto MyOpenThemeData = [](HWND hWnd, LPCWSTR classList) -> HTHEME {
          if (wcscmp(classList, L"ScrollBar") == 0) {
            hWnd = nullptr;
            classList = L"Explorer::ScrollBar";
          }
          return _OpenNcThemeData(hWnd, classList);
        };
        addr->u1.Function = reinterpret_cast<ULONG_PTR>(static_cast<fnOpenNcThemeData>(MyOpenThemeData));
        VirtualProtect(addr, sizeof(IMAGE_THUNK_DATA), oldProtect, &oldProtect);
      }
    }
  }
}
// ============================================================================


constexpr bool CheckBuildNumber(DWORD buildNumber) {
  return (buildNumber == 17763 || // 1809
          buildNumber == 18362 || // 1903
          buildNumber == 18363 || // 1909
          buildNumber == 19041);  // 2004
}


extern "C" void InitDarkMode()
{
  DWORD major, minor;
  DWORD const buildNumber = GetWindowsBuildNumber(&major, &minor);
  if (buildNumber) {
    // undocumented function addresses are only valid for this WinVer build numbers
    if (major == 10 && minor == 0 && CheckBuildNumber(buildNumber))
    {
      HMODULE const hUxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
      if (hUxtheme)
      {
        _OpenNcThemeData = reinterpret_cast<fnOpenNcThemeData>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(49)));
        _RefreshImmersiveColorPolicyState = reinterpret_cast<fnRefreshImmersiveColorPolicyState>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(104)));
        _GetIsImmersiveColorUsingHighContrast = reinterpret_cast<fnGetIsImmersiveColorUsingHighContrast>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(106)));
        _ShouldAppsUseDarkMode = reinterpret_cast<fnShouldAppsUseDarkMode>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(132)));
        _AllowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133)));

        auto const ord135 = GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
        if (buildNumber < 18334)
          _AllowDarkModeForApp = reinterpret_cast<fnAllowDarkModeForApp>(ord135);
        else
          _SetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(ord135);

        //_FlushMenuThemes = reinterpret_cast<fnFlushMenuThemes>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136)));
        _IsDarkModeAllowedForWindow = reinterpret_cast<fnIsDarkModeAllowedForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(137)));

        _SetWindowCompositionAttribute = reinterpret_cast<fnSetWindowCompositionAttribute>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));

        if (_OpenNcThemeData &&
          _RefreshImmersiveColorPolicyState &&
          _ShouldAppsUseDarkMode &&
          _AllowDarkModeForWindow &&
          (_AllowDarkModeForApp || _SetPreferredAppMode) &&
          //_FlushMenuThemes &&
          _IsDarkModeAllowedForWindow)
        {
          s_bDarkModeSupported = true;

          AllowDarkModeForApp(true);

          _RefreshImmersiveColorPolicyState();

          s_bDarkModeSupported = _ShouldAppsUseDarkMode() && !IsHighContrast();

          _FixDarkScrollBar();
        }
      }
    }
  }
}

#endif

// ============================================================================

