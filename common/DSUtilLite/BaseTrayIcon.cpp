/*
 *      Copyright (C) 2010-2012 Hendrik Leppkes
 *      http://www.1f0.de
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "stdafx.h"
#include "BaseTrayIcon.h"
#include "BaseDSPropPage.h"

#include <time.h>
#include <process.h>

#define TRAYICON       0x1f0
#define MSG_TRAYICON   WM_USER + 1
#define MSG_QUIT       WM_USER + 2

CBaseTrayIcon::CBaseTrayIcon(IBaseFilter *pFilter, const WCHAR *wszName, int resIcon)
  : m_hWnd(0)
  , m_hThread(0)
  , m_pFilter(pFilter)
  , m_wszName(wszName)
  , m_resIcon(resIcon)
  , m_bPropPageOpen(FALSE)
  , m_evSetupFinished(TRUE)
{
  memset(&m_NotifyIconData, 0, sizeof(m_NotifyIconData));
  m_evSetupFinished.Reset();
  StartMessageThread();
}

CBaseTrayIcon::~CBaseTrayIcon(void)
{
  if (m_NotifyIconData.uID)
    Shell_NotifyIcon(NIM_DELETE, &m_NotifyIconData);
  if (m_NotifyIconData.hIcon)
    DestroyIcon(m_NotifyIconData.hIcon);
  if (m_hWnd)
    SendMessage(m_hWnd, MSG_QUIT, 0, 0);
  if (m_hThread)
    CloseHandle(m_hThread);
  UnregisterClass(L"LAVTrayIconClass", g_hInst);
}

HRESULT CBaseTrayIcon::StartMessageThread()
{
  m_hThread = (HANDLE)_beginthreadex(NULL,                         /* Security */
                                     0,                            /* Stack Size */
                                     InitialThreadProc,            /* Thread process */
                                     (LPVOID)this,                 /* Arguments */
                                     0,                            /* 0 = Start Immediately */
                                     NULL                          /* Thread Address */
                                     );
  m_evSetupFinished.Wait();
  return S_OK;
}

unsigned int WINAPI CBaseTrayIcon::InitialThreadProc(LPVOID pv)
{
  HRESULT hrCo = CoInitialize(NULL);

  CBaseTrayIcon *pTrayIcon = (CBaseTrayIcon *) pv;
  unsigned int ret = pTrayIcon->TrayMessageThread();

  if (SUCCEEDED(hrCo))
    CoUninitialize();

  return ret;
}

DWORD CBaseTrayIcon::TrayMessageThread()
{
  HRESULT hr;
  MSG msg;

  // Create the Window Class if it doesn't exist yet
  hr = RegisterWindowClass();
  if (FAILED(hr)) {
    DbgLog((LOG_TRACE, 10, L"CBaseTrayIcon::ThreadProc(): Failed to register window class"));
  }

  // And the Window we use for messages
  if (!m_hWnd) {
    hr = CreateMessageWindow();
    if (FAILED(hr)) {
      DbgLog((LOG_TRACE, 10, L"CBaseTrayIcon::ThreadProc(): Failed to create message window"));
      return 1;
    }
  }
  ASSERT(m_hWnd);

  CreateTrayIconData();
  Shell_NotifyIcon(NIM_ADD, &m_NotifyIconData);
  Shell_NotifyIcon(NIM_SETVERSION, &m_NotifyIconData);

  m_evSetupFinished.Set();

  // Message loop
  while (GetMessage(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  return 0;
}

HRESULT CBaseTrayIcon::RegisterWindowClass()
{
  static const WCHAR* wszClassName = L"LAVTrayIconClass";

  WNDCLASSEX wx = {};
  wx.cbSize = sizeof(WNDCLASSEX);
  wx.lpfnWndProc = WindowProc;
  wx.hInstance = g_hInst;
  wx.lpszClassName = wszClassName;
  ATOM wndClass = RegisterClassEx(&wx);

  return wndClass == NULL ? E_FAIL : S_OK;
}

HRESULT CBaseTrayIcon::CreateMessageWindow()
{
  m_hWnd = CreateWindowEx(0, L"LAVTrayIconClass", L"LAV Tray Message Window", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
  SetWindowLongPtr(m_hWnd, GWLP_USERDATA, LONG_PTR(this));
  return m_hWnd == NULL ? E_FAIL : S_OK;
}

HRESULT CBaseTrayIcon::CreateTrayIconData()
{
  memset(&m_NotifyIconData, 0, sizeof(m_NotifyIconData));
  m_NotifyIconData.cbSize = sizeof(m_NotifyIconData);
  m_NotifyIconData.hWnd = m_hWnd;
  m_NotifyIconData.uID = TRAYICON;
  m_NotifyIconData.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
  m_NotifyIconData.hIcon = (HICON)LoadImage(g_hInst, MAKEINTRESOURCE(m_resIcon), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
  m_NotifyIconData.uCallbackMessage = MSG_TRAYICON;
  m_NotifyIconData.uVersion = NOTIFYICON_VERSION;
  wcscpy_s(m_NotifyIconData.szTip, m_wszName);

  return S_OK;
}

LRESULT CALLBACK CBaseTrayIcon::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  CBaseTrayIcon *icon = (CBaseTrayIcon *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
  switch(uMsg) {
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  case MSG_QUIT:
    DestroyWindow(hwnd);
    break;
  case MSG_TRAYICON:
    {
      UINT trayMsg = LOWORD(lParam);
      if (icon) {
        switch (trayMsg) {
        case WM_LBUTTONUP:
          if (!icon->m_bPropPageOpen) {
            icon->m_bPropPageOpen = TRUE;
            CBaseDSPropPage::ShowPropPageDialog(icon->m_pFilter, icon->m_hWnd);
            icon->m_bPropPageOpen = FALSE;
          }
          break;
        }
      }
    }
    break;
  }
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}