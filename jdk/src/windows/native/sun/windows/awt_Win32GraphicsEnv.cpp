/*
 * Copyright 1999-2008 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Sun designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Sun in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 */

#include <windows.h>
#include <jni.h>
#include <awt.h>
#include <sun_awt_Win32GraphicsEnvironment.h>
#include "awt_Canvas.h"
#include "awt_Win32GraphicsDevice.h"
#include "Devices.h"
#include "WindowsFlags.h"

BOOL DWMIsCompositionEnabled();

void initScreens(JNIEnv *env) {

    if (!Devices::UpdateInstance(env)) {
        JNU_ThrowInternalError(env, "Could not update the devices array.");
        return;
    }
}

/**
 * This function attempts to make a Win32 API call to
 *   BOOL SetProcessDPIAware(VOID);
 * which is only present on Windows Vista, and which instructs the
 * Vista Windows Display Manager that this application is High DPI Aware
 * and does not need to be scaled by the WDM and lied about the
 * actual system dpi.
 */
static void
SetProcessDPIAwareProperty()
{
    typedef BOOL (WINAPI SetProcessDPIAwareFunc)(void);
    static BOOL bAlreadySet = FALSE;

    // setHighDPIAware is set in WindowsFlags.cpp
    if (!setHighDPIAware || bAlreadySet) {
        return;
    }

    bAlreadySet = TRUE;

    HMODULE hLibUser32Dll = ::LoadLibrary(TEXT("user32.dll"));

    if (hLibUser32Dll != NULL) {
        SetProcessDPIAwareFunc *lpSetProcessDPIAware =
            (SetProcessDPIAwareFunc*)GetProcAddress(hLibUser32Dll,
                                                    "SetProcessDPIAware");
        if (lpSetProcessDPIAware != NULL) {
            lpSetProcessDPIAware();
        }
        ::FreeLibrary(hLibUser32Dll);
    }
}

#define DWM_COMP_UNDEFINED (~(TRUE|FALSE))
static int dwmIsCompositionEnabled = DWM_COMP_UNDEFINED;

/**
 * This function is called from toolkit event handling code when
 * WM_DWMCOMPOSITIONCHANGED event is received
 */
void DWMResetCompositionEnabled() {
    dwmIsCompositionEnabled = DWM_COMP_UNDEFINED;
    (void)DWMIsCompositionEnabled();
}

/**
 * Returns true if dwm composition is enabled, false if it is not applicable
 * (if the OS is not Vista) or dwm composition is disabled.
 *
 * Note: since DWM composition state changes are very rare we load/unload the
 * dll on every change.
 */
BOOL DWMIsCompositionEnabled() {
    typedef HRESULT (WINAPI DwmIsCompositionEnabledFunc)(BOOL*);

    // cheaper to check than whether it's vista or not
    if (dwmIsCompositionEnabled != DWM_COMP_UNDEFINED) {
        return (BOOL)dwmIsCompositionEnabled;
    }

    if (!IS_WINVISTA) {
        dwmIsCompositionEnabled = FALSE;
        return FALSE;
    }

    BOOL bRes = FALSE;
    HMODULE hDwmApiDll = ::LoadLibrary(TEXT("dwmapi.dll"));

    if (hDwmApiDll != NULL) {
        DwmIsCompositionEnabledFunc *lpDwmIsCompEnabled =
            (DwmIsCompositionEnabledFunc*)
                GetProcAddress(hDwmApiDll, "DwmIsCompositionEnabled");
        if (lpDwmIsCompEnabled != NULL) {
            BOOL bEnabled;
            HRESULT res = lpDwmIsCompEnabled(&bEnabled);
            if (SUCCEEDED(res)) {
                bRes = bEnabled;
                J2dTraceLn1(J2D_TRACE_VERBOSE, " composition enabled: %d",bRes);
            } else {
                J2dTraceLn1(J2D_TRACE_ERROR,
                            "IsDWMCompositionEnabled: error %x when detecting"\
                            "if composition is enabled", res);
            }
        } else {
            J2dTraceLn(J2D_TRACE_ERROR,
                       "IsDWMCompositionEnabled: no DwmIsCompositionEnabled() "\
                       "in dwmapi.dll");
        }
        ::FreeLibrary(hDwmApiDll);
    } else {
        J2dTraceLn(J2D_TRACE_ERROR,
                   "IsDWMCompositionEnabled: error opening dwmapi.dll");
    }

    dwmIsCompositionEnabled = bRes;

    JNIEnv *env = (JNIEnv *)JNU_GetEnv(jvm, JNI_VERSION_1_2);
    JNU_CallStaticMethodByName(env, NULL,
                              "sun/awt/Win32GraphicsEnvironment",
                              "dwmCompositionChanged", "(Z)V", (jboolean)bRes);
    return bRes;
}

/*
 * Class:     sun_awt_Win32GraphicsEnvironment
 * Method:    initDisplay
 * Signature: ()V
 */
JNIEXPORT void JNICALL
Java_sun_awt_Win32GraphicsEnvironment_initDisplay(JNIEnv *env,
                                                  jclass thisClass)
{
    // This method needs to be called prior to any display-related activity
    SetProcessDPIAwareProperty();

    DWMIsCompositionEnabled();

    initScreens(env);
}

/*
 * Class:     sun_awt_Win32GraphicsEnvironment
 * Method:    getNumScreens
 * Signature: ()I
 */
JNIEXPORT jint JNICALL
Java_sun_awt_Win32GraphicsEnvironment_getNumScreens(JNIEnv *env,
                                                    jobject thisobj)
{
    Devices::InstanceAccess devices;
    return devices->GetNumDevices();
}

/*
 * Class:     sun_awt_Win32GraphicsEnvironment
 * Method:    getDefaultScreen
 * Signature: ()I
 */
JNIEXPORT jint JNICALL
Java_sun_awt_Win32GraphicsEnvironment_getDefaultScreen(JNIEnv *env,
                                                       jobject thisobj)
{
    return AwtWin32GraphicsDevice::GetDefaultDeviceIndex();
}

#define FR_PRIVATE 0x10 /* from wingdi.h */
typedef int (WINAPI *AddFontResourceExType)(LPCTSTR,DWORD,VOID*);
typedef int (WINAPI *RemoveFontResourceExType)(LPCTSTR,DWORD,VOID*);

static AddFontResourceExType procAddFontResourceEx = NULL;
static RemoveFontResourceExType procRemoveFontResourceEx = NULL;

static int winVer = -1;

static int getWinVer() {
    if (winVer == -1) {
        OSVERSIONINFO osvi;
        osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
        GetVersionEx(&osvi);
        winVer = osvi.dwMajorVersion;
        if (winVer >= 5) {
          // REMIND verify on 64 bit windows
          HMODULE hGDI = LoadLibrary(TEXT("gdi32.dll"));
          if (hGDI != NULL) {
            procAddFontResourceEx =
              (AddFontResourceExType)GetProcAddress(hGDI,"AddFontResourceExW");
            if (procAddFontResourceEx == NULL) {
              winVer = 0;
            }
            procRemoveFontResourceEx =
              (RemoveFontResourceExType)GetProcAddress(hGDI,
                                                      "RemoveFontResourceExW");
            if (procRemoveFontResourceEx == NULL) {
              winVer = 0;
            }
            FreeLibrary(hGDI);
          }
        }
    }

    return winVer;
}

/*
 * Class:     sun_awt_Win32GraphicsEnvironment
 * Method:    registerFontWithPlatform
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL
Java_sun_awt_Win32GraphicsEnvironment_registerFontWithPlatform(JNIEnv *env,
                                                              jclass cl,
                                                              jstring fontName)
{
    if (getWinVer() >= 5 && procAddFontResourceEx != NULL) {
      LPTSTR file = (LPTSTR)JNU_GetStringPlatformChars(env, fontName, NULL);
      (*procAddFontResourceEx)(file, FR_PRIVATE, NULL);
    }
}


/*
 * Class:     sun_awt_Win32GraphicsEnvironment
 * Method:    deRegisterFontWithPlatform
 * Signature: (Ljava/lang/String;)V
 *
 * This method intended for future use.
 */
JNIEXPORT void JNICALL
Java_sun_awt_Win32GraphicsEnvironment_deRegisterFontWithPlatform(JNIEnv *env,
                                                              jclass cl,
                                                              jstring fontName)
{
    if (getWinVer() >= 5 && procRemoveFontResourceEx != NULL) {
      LPTSTR file = (LPTSTR)JNU_GetStringPlatformChars(env, fontName, NULL);
      (*procRemoveFontResourceEx)(file, FR_PRIVATE, NULL);
    }
}

#define EUDCKEY_JA_JP  L"EUDC\\932"
#define EUDCKEY_ZH_CN  L"EUDC\\936"
#define EUDCKEY_ZH_TW  L"EUDC\\950"
#define EUDCKEY_KO_KR  L"EUDC\\949"
#define LANGID_JA_JP   0x411
#define LANGID_ZH_CN   0x0804
#define LANGID_ZH_SG   0x1004
#define LANGID_ZH_TW   0x0404
#define LANGID_ZH_HK   0x0c04
#define LANGID_ZH_MO   0x1404
#define LANGID_KO_KR   0x0412


JNIEXPORT jstring JNICALL
Java_sun_awt_Win32GraphicsEnvironment_getEUDCFontFile(JNIEnv *env, jclass cl) {
    int    rc;
    HKEY   key;
    DWORD  type;
    WCHAR  fontPathBuf[MAX_PATH + 1];
    unsigned long fontPathLen = MAX_PATH + 1;
    WCHAR  tmpPath[MAX_PATH + 1];
    LPWSTR fontPath = fontPathBuf;
    LPWSTR eudcKey = NULL;

    LANGID langID = GetSystemDefaultLangID();
    //lookup for encoding ID, EUDC only supported in
    //codepage 932, 936, 949, 950 (and unicode)
    if (langID == LANGID_JA_JP) {
        eudcKey = EUDCKEY_JA_JP;
    } else if (langID == LANGID_ZH_CN || langID == LANGID_ZH_SG) {
        eudcKey = EUDCKEY_ZH_CN;
    } else if (langID == LANGID_ZH_HK || langID == LANGID_ZH_TW ||
               langID == LANGID_ZH_MO) {
      eudcKey = EUDCKEY_ZH_TW;
    } else if (langID == LANGID_KO_KR) {
        eudcKey = EUDCKEY_KO_KR;
    } else {
        return NULL;
    }

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, eudcKey, 0, KEY_READ, &key);
    if (rc != ERROR_SUCCESS) {
        return NULL;
    }
    rc = RegQueryValueEx(key,
                         L"SystemDefaultEUDCFont",
                         0,
                         &type,
                         (LPBYTE)fontPath,
                         &fontPathLen);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_SZ) {
        return NULL;
    }
    fontPath[fontPathLen] = L'\0';
    if (wcsstr(fontPath, L"%SystemRoot%")) {
        //if the fontPath includes %SystemRoot%
        LPWSTR systemRoot = _wgetenv(L"SystemRoot");
        if (systemRoot != NULL
            && swprintf(tmpPath, L"%s%s", systemRoot, fontPath + 12) != -1) {
            fontPath = tmpPath;
        }
        else {
            return NULL;
        }
    } else if (wcscmp(fontPath, L"EUDC.TTE") == 0) {
        //else to see if it only inludes "EUDC.TTE"
        WCHAR systemRoot[MAX_PATH + 1];
        if (GetWindowsDirectory(systemRoot, MAX_PATH + 1) != 0) {
            swprintf(tmpPath, L"%s\\FONTS\\EUDC.TTE", systemRoot);
            fontPath = tmpPath;
        }
        else {
            return NULL;
        }
    }
    return JNU_NewStringPlatform(env, fontPath);
}

/*
 * Class:     sun_awt_Win32GraphicsEnvironment
 * Method:    getXResolution
 * Signature: ()I
 */
JNIEXPORT jint JNICALL
Java_sun_awt_Win32GraphicsEnvironment_getXResolution(JNIEnv *env, jobject wge)
{
    TRY;

    HWND hWnd = ::GetDesktopWindow();
    HDC hDC = ::GetDC(hWnd);
    jint result = ::GetDeviceCaps(hDC, LOGPIXELSX);
    ::ReleaseDC(hWnd, hDC);
    return result;

    CATCH_BAD_ALLOC_RET(0);
}

/*
 * Class:     sun_awt_Win32GraphicsEnvironment
 * Method:    getYResolution
 * Signature: ()I
 */
JNIEXPORT jint JNICALL
Java_sun_awt_Win32GraphicsEnvironment_getYResolution(JNIEnv *env, jobject wge)
{
    TRY;

    HWND hWnd = ::GetDesktopWindow();
    HDC hDC = ::GetDC(hWnd);
    jint result = ::GetDeviceCaps(hDC, LOGPIXELSY);
    ::ReleaseDC(hWnd, hDC);
    return result;

    CATCH_BAD_ALLOC_RET(0);
}

/*
 * Class:     sun_awt_Win32GraphicsEnvironment
 * Method:    isVistaOS
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_sun_awt_Win32GraphicsEnvironment_isVistaOS
  (JNIEnv *env, jclass wgeclass)
{
    return IS_WINVISTA;
}
