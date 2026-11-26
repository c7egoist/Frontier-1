/*============================================================================================================================================
                                                           SOLIDARCLAUNCH.CPP
============================================================================================================================================*/
// 🧩 Standalone SolidArc executable: launches native GLFW + Vulkan desktop window or interactive POSIX WebSocket stream (--serve=<port>)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Lifecycle/DesktopWorkspaceLaunch.h"
#include "Engine/Lifecycle/ThoughtLog.h"

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Editor/Explorer/ContentBrowser.h"
#include "Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Input/InputExtension.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocumentCodec.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherExchange.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherWorkspaceHost.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

#ifndef FRONTIER_COMPILED_SHADER_DIR
#define FRONTIER_COMPILED_SHADER_DIR "CompiledShaders"
#endif

#if defined(__unix__) || defined(__APPLE__)
namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                         RFC 6455 WEBSOCKET HANDSHAKE DIGEST
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint32_t RotateLeft32(uint32_t Word, uint32_t Bits) noexcept
{
    return (Word << Bits) | (Word >> (32u - Bits));
}

[[nodiscard]] std::array<uint8_t, 20u> ComputeSha1Digest(std::string_view Input) noexcept
{
    uint32_t H0 = 0x67452301u;
    uint32_t H1 = 0xEFCDAB89u;
    uint32_t H2 = 0x98BADCFEu;
    uint32_t H3 = 0x10325476u;
    uint32_t H4 = 0xC3D2E1F0u;

    std::vector<uint8_t> Padded(Input.begin(), Input.end());
    const uint64_t BitLength = static_cast<uint64_t>(Padded.size()) * 8ull;
    Padded.push_back(0x80u);
    while ((Padded.size() % 64u) != 56u)
    {
        Padded.push_back(0x00u);
    }
    for (int Shift = 56; Shift >= 0; Shift -= 8)
    {
        Padded.push_back(static_cast<uint8_t>((BitLength >> Shift) & 0xFFull));
    }

    for (std::size_t ChunkStart = 0u; ChunkStart < Padded.size(); ChunkStart += 64u)
    {
        std::array<uint32_t, 80u> Words {};
        for (std::size_t Idx = 0u; Idx < 16u; ++Idx)
        {
            const std::size_t Off = ChunkStart + Idx * 4u;
            Words[Idx] = (static_cast<uint32_t>(Padded[Off]) << 24u) |
                         (static_cast<uint32_t>(Padded[Off + 1u]) << 16u) |
                         (static_cast<uint32_t>(Padded[Off + 2u]) << 8u) |
                         static_cast<uint32_t>(Padded[Off + 3u]);
        }
        for (std::size_t Idx = 16u; Idx < 80u; ++Idx)
        {
            Words[Idx] = RotateLeft32(Words[Idx - 3u] ^ Words[Idx - 8u] ^ Words[Idx - 14u] ^ Words[Idx - 16u], 1u);
        }

        uint32_t A = H0;
        uint32_t B = H1;
        uint32_t C = H2;
        uint32_t D = H3;
        uint32_t E = H4;

        for (std::size_t Idx = 0u; Idx < 80u; ++Idx)
        {
            uint32_t Mix = 0u;
            uint32_t Key = 0u;
            if (Idx < 20u)
            {
                Mix = (B & C) | ((~B) & D);
                Key = 0x5A827999u;
            }
            else if (Idx < 40u)
            {
                Mix = B ^ C ^ D;
                Key = 0x6ED9EBA1u;
            }
            else if (Idx < 60u)
            {
                Mix = (B & C) | (B & D) | (C & D);
                Key = 0x8F1BBCDCu;
            }
            else
            {
                Mix = B ^ C ^ D;
                Key = 0xCA62C1D6u;
            }
            const uint32_t NextA = RotateLeft32(A, 5u) + Mix + E + Key + Words[Idx];
            E = D;
            D = C;
            C = RotateLeft32(B, 30u);
            B = A;
            A = NextA;
        }

        H0 += A;
        H1 += B;
        H2 += C;
        H3 += D;
        H4 += E;
    }

    std::array<uint8_t, 20u> Digest {};
    const std::array<uint32_t, 5u> Finals { H0, H1, H2, H3, H4 };
    for (std::size_t Idx = 0u; Idx < 5u; ++Idx)
    {
        Digest[Idx * 4u + 0u] = static_cast<uint8_t>((Finals[Idx] >> 24u) & 0xFFu);
        Digest[Idx * 4u + 1u] = static_cast<uint8_t>((Finals[Idx] >> 16u) & 0xFFu);
        Digest[Idx * 4u + 2u] = static_cast<uint8_t>((Finals[Idx] >> 8u) & 0xFFu);
        Digest[Idx * 4u + 3u] = static_cast<uint8_t>(Finals[Idx] & 0xFFu);
    }
    return Digest;
}

[[nodiscard]] std::string EncodeSixBitDigest(const std::array<uint8_t, 20u>& Bytes)
{
    static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string Encoded {};
    Encoded.reserve(28u);
    for (std::size_t Idx = 0u; Idx < 20u; Idx += 3u)
    {
        const uint32_t B0 = Bytes[Idx];
        const uint32_t B1 = (Idx + 1u < 20u) ? Bytes[Idx + 1u] : 0u;
        const uint32_t B2 = (Idx + 2u < 20u) ? Bytes[Idx + 2u] : 0u;
        const uint32_t Triple = (B0 << 16u) | (B1 << 8u) | B2;
        Encoded.push_back(Alphabet[(Triple >> 18u) & 0x3Fu]);
        Encoded.push_back(Alphabet[(Triple >> 12u) & 0x3Fu]);
        Encoded.push_back((Idx + 1u < 20u) ? Alphabet[(Triple >> 6u) & 0x3Fu] : '=');
        Encoded.push_back((Idx + 2u < 20u) ? Alphabet[Triple & 0x3Fu] : '=');
    }
    return Encoded;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      EMBEDDED HTML5 INTERACTIVE SOLIDARC CLIENT
//------------------------------------------------------------------------------------------------------------------------

static constexpr const char EmbeddedClientHtml[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>SolidArc Standalone — Live Native Vulkan + C++20 Session</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  html, body {
    width: 100%; height: 100%; overflow: hidden;
    background: #0b0c0e; color: #f1f3f5;
    font-family: "Inter", -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    user-select: none; -webkit-user-select: none;
  }
  #viewport-canvas {
    display: block; width: 100vw; height: 100vh;
    cursor: default; outline: none;
  }
  /* Boot & Diagnostic Overlay */
  #boot-overlay {
    position: fixed; inset: 0; z-index: 40;
    background: #0b0c0e;
    display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 14px;
    transition: opacity 0.22s ease;
  }
  #boot-overlay.hidden { opacity: 0; pointer-events: none; }
  .boot-card {
    width: min(440px, 90vw); padding: 22px 24px;
    background: #141619; border: 1px solid rgba(255,255,255,0.14);
    border-radius: 20px; display: flex; flex-direction: column; gap: 12px;
  }
  .boot-title { font-size: 14px; font-weight: 600; display: flex; justify-content: space-between; align-items: center; }
  .boot-sub { font-size: 12px; color: #8e959e; font-family: "JetBrains Mono", monospace; }
  .boot-track { width: 100%; height: 5px; background: #23262b; border-radius: 999px; overflow: hidden; }
  .boot-fill { height: 100%; width: 25%; background: linear-gradient(90deg, #4ade80, #ffffff); transition: width 0.2s ease; }
  #error-banner {
    position: fixed; top: 12px; left: 50%; transform: translateX(-50%); z-index: 50;
    display: none; align-items: center; gap: 12px;
    background: rgba(127, 29, 29, 0.94); border: 1px solid rgba(248, 113, 113, 0.55);
    color: #fecaca; padding: 9px 16px; border-radius: 999px; font-size: 12px;
    backdrop-filter: blur(10px);
  }
  #error-banner.visible { display: inline-flex; }
  /* Compact Floating Bridge Pill (Bottom-Left above status or Top-Center-Right non-blocking) */
  #bridge-hud {
    position: fixed; bottom: 8px; right: 12px; z-index: 30;
    display: inline-flex; align-items: center; gap: 6px;
    padding: 4px 10px; border-radius: 999px;
    background: rgba(20, 22, 25, 0.86); border: 1px solid rgba(255,255,255,0.12);
    backdrop-filter: blur(10px); font-size: 11px; color: #8e959e;
  }
  .hud-dot { width: 6px; height: 6px; border-radius: 50%; background: #4ade80; }
  .hud-btn {
    border: 1px solid rgba(255,255,255,0.14); background: #23262b; color: #f1f3f5;
    font-size: 10.5px; padding: 3px 9px; border-radius: 999px; cursor: pointer;
  }
  .hud-btn:hover { background: #2e3239; }
</style>
</head>
<body>
  <canvas id="viewport-canvas" tabindex="0"></canvas>

  <div id="boot-overlay">
    <div class="boot-card">
      <div class="boot-title">
        <span>SolidArc Standalone (Native C++20 + Vulkan)</span>
        <span id="boot-pct" style="font-family: monospace; color: #4ade80;">25%</span>
      </div>
      <div class="boot-sub" id="boot-msg">Connecting WebSocket stream to SolidArc process…</div>
      <div class="boot-track"><div class="boot-fill" id="boot-fill"></div></div>
    </div>
  </div>

  <div id="error-banner">
    <span id="error-text">Connection interrupted.</span>
    <button class="hud-btn" onclick="connectStream()">Reconnect</button>
  </div>

  <div id="bridge-hud">
    <span class="hud-dot" id="hud-dot"></span>
    <span id="hud-telemetry" style="font-family: monospace;">Connecting…</span>
    <button class="hud-btn" onclick="document.getElementById('file-picker').click()" title="Import .step, .solidarc, .blend, or .toml into SolidArc">Import File…</button>
    <button class="hud-btn" onclick="window.open('/export?ext=.solidarc','_blank')" title="Export current document as unified .solidarc">Save .solidarc</button>
    <button class="hud-btn" onclick="window.open('/export?ext=.step','_blank')" title="Export visible solids as ISO 10303-21 STEP AP214">Export .STEP</button>
    <input type="file" id="file-picker" style="display:none" accept=".solidarc,.step,.stp,.blend,.toml,.png,.jpg" onchange="uploadSelectedFile(this.files[0])" />
  </div>

<script>
  const canvas = document.getElementById('viewport-canvas');
  const ctx = canvas.getContext('2d', { alpha: false });
  let ws = null;
  let frameCounter = 0;
  let lastFpsStamp = performance.now();
  let measuredFps = 0;
  let firstFrameReceived = false;

  function setBootProgress(pct, msg) {
    document.getElementById('boot-pct').textContent = pct + '%';
    document.getElementById('boot-fill').style.width = pct + '%';
    document.getElementById('boot-msg').textContent = msg;
  }

  function showError(msg) {
    const b = document.getElementById('error-banner');
    document.getElementById('error-text').textContent = msg;
    b.classList.add('visible');
    document.getElementById('hud-dot').style.background = '#ef4444';
  }

  function hideError() {
    document.getElementById('error-banner').classList.remove('visible');
    document.getElementById('hud-dot').style.background = '#4ade80';
  }

  function clampViewportDim(w, h) {
    const cw = Math.max(800, Math.min(1920, Math.floor(w / 2) * 2));
    const ch = Math.max(540, Math.min(1160, Math.floor(h / 2) * 2));
    return [cw, ch];
  }

  function sendCmd(line) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(line);
    }
  }

  function syncResize() {
    const [w, h] = clampViewportDim(window.innerWidth, window.innerHeight);
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }
    sendCmd(`R ${w} ${h}`);
  }

  function connectStream() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    setBootProgress(55, 'Opening low-latency Vulkan frame channel…');
    ws = new WebSocket(`${proto}//${location.host}/ws`);
    ws.binaryType = 'arraybuffer';

    ws.onopen = () => {
      hideError();
      setBootProgress(85, 'Awaiting initial Vulkan B-Rep frame…');
      syncResize();
      canvas.focus();
    };

    ws.onmessage = async (ev) => {
      if (typeof ev.data === 'string') {
        if (ev.data.startsWith('ERR ')) showError(ev.data.slice(4));
        return;
      }
      try {
        const blob = new Blob([ev.data], { type: 'image/jpeg' });
        const bmp = await createImageBitmap(blob);
        if (canvas.width !== bmp.width || canvas.height !== bmp.height) {
          canvas.width = bmp.width;
          canvas.height = bmp.height;
        }
        ctx.drawImage(bmp, 0, 0);
        bmp.close();
        sendCmd('A');

        if (!firstFrameReceived) {
          firstFrameReceived = true;
          setBootProgress(100, 'Ready');
          document.getElementById('boot-overlay').classList.add('hidden');
        }

        frameCounter++;
        const now = performance.now();
        if (now - lastFpsStamp >= 600) {
          measuredFps = Math.round((frameCounter * 1000) / (now - lastFpsStamp));
          frameCounter = 0;
          lastFpsStamp = now;
        }
        document.getElementById('hud-telemetry').textContent =
          `Native C++ · ${canvas.width}×${canvas.height} · ${measuredFps > 0 ? measuredFps + ' fps' : 'Live'}`;
      } catch (err) {
        sendCmd('A');
      }
    };

    ws.onerror = () => {
      showError('WebSocket transport error — click Reconnect to resume SolidArc session.');
    };

    ws.onclose = () => {
      showError('SolidArc stream disconnected — click Reconnect.');
    };
  }

  function canvasCoords(e) {
    const rect = canvas.getBoundingClientRect();
    const sx = canvas.width / Math.max(1, rect.width);
    const sy = canvas.height / Math.max(1, rect.height);
    return [
      Math.round((e.clientX - rect.left) * sx * 10) / 10,
      Math.round((e.clientY - rect.top) * sy * 10) / 10
    ];
  }

  canvas.addEventListener('pointermove', (e) => {
    const [x, y] = canvasCoords(e);
    sendCmd(`M ${x} ${y}`);
  });

  canvas.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    canvas.focus();
    canvas.setPointerCapture(e.pointerId);
    const [x, y] = canvasCoords(e);
    sendCmd(`M ${x} ${y}`);
    sendCmd(`B ${e.button} 1`);
  });

  canvas.addEventListener('pointerup', (e) => {
    e.preventDefault();
    const [x, y] = canvasCoords(e);
    sendCmd(`M ${x} ${y}`);
    sendCmd(`B ${e.button} 0`);
  });

  canvas.addEventListener('contextmenu', (e) => e.preventDefault());
  canvas.addEventListener('auxclick', (e) => e.preventDefault());

  canvas.addEventListener('wheel', (e) => {
    e.preventDefault();
    const dy = -Math.sign(e.deltaY) * Math.min(4.0, Math.max(0.35, Math.abs(e.deltaY) / 100.0));
    const dx = -Math.sign(e.deltaX) * Math.min(4.0, Math.abs(e.deltaX) / 100.0);
    sendCmd(`W ${dx.toFixed(2)} ${dy.toFixed(2)}`);
  }, { passive: false });

  window.addEventListener('keydown', (e) => {
    if (document.activeElement !== canvas && document.activeElement !== document.body) return;
    // Prevent browser Tab / Space / Backspace / Ctrl+F / Ctrl+S / Alt menu from stealing CAD hotkeys
    if (e.key === 'Tab' || e.key === 'Backspace' || e.key === 'Alt' || e.key === 'F1' || e.key === 'F2' || e.key === 'F3' ||
        ((e.ctrlKey || e.metaKey) && ['f','s','z','y','a','d','o','n'].includes(e.key.toLowerCase()))) {
      e.preventDefault();
    }
    const ctrl = (e.ctrlKey || e.metaKey) ? 1 : 0;
    const shift = e.shiftKey ? 1 : 0;
    const alt = e.altKey ? 1 : 0;
    sendCmd(`K ${e.code} 1 ${ctrl} ${shift} ${alt}`);
    if (e.key.length === 1 && !e.ctrlKey && !e.metaKey && !e.altKey) {
      sendCmd(`C ${e.key.codePointAt(0)}`);
    }
  });

  window.addEventListener('keyup', (e) => {
    const ctrl = (e.ctrlKey || e.metaKey) ? 1 : 0;
    const shift = e.shiftKey ? 1 : 0;
    const alt = e.altKey ? 1 : 0;
    sendCmd(`K ${e.code} 0 ${ctrl} ${shift} ${alt}`);
  });

  window.addEventListener('blur', () => sendCmd('X'));
  window.addEventListener('resize', () => syncResize());

  async function uploadSelectedFile(file) {
    if (!file) return;
    try {
      const buf = await file.arrayBuffer();
      const res = await fetch('/upload?name=' + encodeURIComponent(file.name), {
        method: 'POST',
        body: buf
      });
      if (!res.ok) {
        const txt = await res.text();
        showError('Import failed: ' + txt);
      } else {
        hideError();
      }
    } catch (err) {
      showError('Upload error: ' + err.message);
    }
  }

  window.addEventListener('dragover', (e) => e.preventDefault());
  window.addEventListener('drop', (e) => {
    e.preventDefault();
    if (e.dataTransfer && e.dataTransfer.files.length > 0) {
      uploadSelectedFile(e.dataTransfer.files[0]);
    }
  });

  connectStream();
</script>
</body>
</html>
)HTML";

//------------------------------------------------------------------------------------------------------------------------
//                                        BROWSER CODE TO IMGUI KEY TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ImGuiKey BrowserCodeToImGuiKey(std::string_view Code) noexcept
{
    if (Code.size() == 4u && Code.starts_with("Key"))
    {
        const char Ch = Code[3u];
        if (Ch >= 'A' && Ch <= 'Z')
        {
            return static_cast<ImGuiKey>(ImGuiKey_A + (Ch - 'A'));
        }
    }
    if (Code.size() == 6u && Code.starts_with("Digit"))
    {
        const char Ch = Code[5u];
        if (Ch >= '0' && Ch <= '9')
        {
            return static_cast<ImGuiKey>(ImGuiKey_0 + (Ch - '0'));
        }
    }
    if (Code == "Escape") return ImGuiKey_Escape;
    if (Code == "Enter" || Code == "NumpadEnter") return ImGuiKey_Enter;
    if (Code == "Tab") return ImGuiKey_Tab;
    if (Code == "Backspace") return ImGuiKey_Backspace;
    if (Code == "Delete") return ImGuiKey_Delete;
    if (Code == "Space") return ImGuiKey_Space;
    if (Code == "ArrowLeft") return ImGuiKey_LeftArrow;
    if (Code == "ArrowRight") return ImGuiKey_RightArrow;
    if (Code == "ArrowUp") return ImGuiKey_UpArrow;
    if (Code == "ArrowDown") return ImGuiKey_DownArrow;
    if (Code == "Home") return ImGuiKey_Home;
    if (Code == "End") return ImGuiKey_End;
    if (Code == "PageUp") return ImGuiKey_PageUp;
    if (Code == "PageDown") return ImGuiKey_PageDown;
    if (Code == "Minus" || Code == "NumpadSubtract") return ImGuiKey_Minus;
    if (Code == "Equal" || Code == "NumpadAdd") return ImGuiKey_Equal;
    if (Code == "Comma") return ImGuiKey_Comma;
    if (Code == "Period" || Code == "NumpadDecimal") return ImGuiKey_Period;
    if (Code == "Slash" || Code == "NumpadDivide") return ImGuiKey_Slash;
    if (Code == "BracketLeft") return ImGuiKey_LeftBracket;
    if (Code == "BracketRight") return ImGuiKey_RightBracket;
    if (Code == "ShiftLeft") return ImGuiKey_LeftShift;
    if (Code == "ShiftRight") return ImGuiKey_RightShift;
    if (Code == "ControlLeft" || Code == "MetaLeft") return ImGuiKey_LeftCtrl;
    if (Code == "ControlRight" || Code == "MetaRight") return ImGuiKey_RightCtrl;
    if (Code == "AltLeft") return ImGuiKey_LeftAlt;
    if (Code == "AltRight") return ImGuiKey_RightAlt;
    if (Code == "F1") return ImGuiKey_F1;
    if (Code == "F2") return ImGuiKey_F2;
    if (Code == "F3") return ImGuiKey_F3;
    if (Code == "F4") return ImGuiKey_F4;
    if (Code == "F5") return ImGuiKey_F5;
    if (Code == "F6") return ImGuiKey_F6;
    return ImGuiKey_None;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      SOCKET STREAMING & LATCHED INPUT DISPATCH
//------------------------------------------------------------------------------------------------------------------------

enum class QueuedInputCategory : uint8_t
{
    PointerMove   = 0u,
    PointerButton = 1u,
    WheelScroll   = 2u,
    KeyTransition = 3u,
    TextCharacter = 4u,
    ReleaseAll    = 5u,
};

struct QueuedInputEvent
{
    QueuedInputCategory Category = QueuedInputCategory::PointerMove;
    float               AxisX    = 0.0f;
    float               AxisY    = 0.0f;
    int                 Code     = 0;
    bool                Pressed  = false;
    bool                CtrlMod  = false;
    bool                ShiftMod = false;
    bool                AltMod   = false;
};

struct StreamClientConnection
{
    int                  SocketFd      = -1;
    bool                 UpgradedWs    = false;
    uint32_t             InFlightCount = 0u;
    bool                 NeedsInitial  = false;
    std::vector<uint8_t> ReceiveBuffer {};
};

bool WriteAllBytes(int Fd, const void* Buffer, std::size_t Length) noexcept
{
    const auto* Cursor = static_cast<const uint8_t*>(Buffer);
    std::size_t Remaining = Length;
    while (Remaining > 0u)
    {
        const ssize_t Sent = ::send(Fd, Cursor, Remaining, MSG_NOSIGNAL);
        if (Sent > 0)
        {
            Cursor    += static_cast<std::size_t>(Sent);
            Remaining -= static_cast<std::size_t>(Sent);
        }
        else
        {
            return false;
        }
    }
    return true;
}

bool SendWebSocketBinary(int Fd, const std::vector<uint8_t>& Payload) noexcept
{
    std::array<uint8_t, 10u> Header {};
    std::size_t HeaderLen = 0u;
    Header[0u] = 0x82u; // FIN + Binary opcode
    if (Payload.size() < 126u)
    {
        Header[1u] = static_cast<uint8_t>(Payload.size());
        HeaderLen  = 2u;
    }
    else if (Payload.size() <= 65535u)
    {
        Header[1u] = 126u;
        Header[2u] = static_cast<uint8_t>((Payload.size() >> 8u) & 0xFFu);
        Header[3u] = static_cast<uint8_t>(Payload.size() & 0xFFu);
        HeaderLen  = 4u;
    }
    else
    {
        Header[1u] = 127u;
        const uint64_t Len64 = Payload.size();
        for (std::size_t B = 0u; B < 8u; ++B)
        {
            Header[2u + B] = static_cast<uint8_t>((Len64 >> ((7u - B) * 8u)) & 0xFFull);
        }
        HeaderLen = 10u;
    }
    return WriteAllBytes(Fd, Header.data(), HeaderLen) &&
           WriteAllBytes(Fd, Payload.data(), Payload.size());
}

[[nodiscard]] std::string DecodeUrlComponent(std::string_view Encoded)
{
    std::string Decoded {};
    Decoded.reserve(Encoded.size());
    for (std::size_t Idx = 0u; Idx < Encoded.size(); ++Idx)
    {
        if (Encoded[Idx] == '%' && Idx + 2u < Encoded.size())
        {
            char Hex[3] { Encoded[Idx + 1u], Encoded[Idx + 2u], '\0' };
            Decoded.push_back(static_cast<char>(std::strtol(Hex, nullptr, 16)));
            Idx += 2u;
        }
        else if (Encoded[Idx] == '+')
        {
            Decoded.push_back(' ');
        }
        else
        {
            Decoded.push_back(Encoded[Idx]);
        }
    }
    return Decoded;
}

void AppendJpegBytesCallback(void* Context, void* Chunk, int ChunkSize)
{
    if (Context != nullptr && Chunk != nullptr && ChunkSize > 0)
    {
        auto* Dest = static_cast<std::vector<uint8_t>*>(Context);
        const auto* Bytes = static_cast<const uint8_t*>(Chunk);
        Dest->insert(Dest->end(), Bytes, Bytes + ChunkSize);
    }
}

int RunInteractiveStreamServer(
    uint16_t                     ListenPort,
    const std::filesystem::path& RepositoryRootPath,
    const std::filesystem::path& CompiledShaderDir,
    const std::filesystem::path& StartupImportPath)
{
    ::signal(SIGPIPE, SIG_IGN);

    Frontier::Diagnostics::ThoughtLog::Initialize();
    Frontier::Input::InputExtension::AssignActiveExtension(nullptr);

    Frontier::GeometricRaster::VulkanInterchange DeviceInterchange {};
    std::string VkDiag {};
    if (!DeviceInterchange.InitializeInterchange(RepositoryRootPath, &VkDiag))
    {
        std::fprintf(stderr, "[SolidArc] Vulkan initialization failed: %s\n", VkDiag.c_str());
        return 1;
    }
    std::fprintf(
        stdout,
        "[SolidArc] Active Vulkan Adapter: %.*s\n",
        static_cast<int>(DeviceInterchange.QueryAdapterName().size()),
        DeviceInterchange.QueryAdapterName().data());
    std::fflush(stdout);

    uint32_t SurfaceW = 1440u;
    uint32_t SurfaceH = 900u;

    IMGUI_CHECKVERSION();
    ImGuiContext* GuiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiCtx);
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    Io.IniFilename  = nullptr;
    Io.DisplaySize  = ImVec2(static_cast<float>(SurfaceW), static_cast<float>(SurfaceH));
    Io.DeltaTime    = 1.0f / 60.0f;

    Frontier::TypographyRepository Typography {};
    if (Typography.LoadRepository(RepositoryRootPath / "EngineContent/FontsArchives") > 0u)
    {
        Frontier::TypographyRepository::AssignCurrent(&Typography);
        if (ImFont* TabFont = Typography.QueryTabStripFont())
        {
            Io.FontDefault = TabFont;
        }
    }

    Frontier::ControlCentrePanel ControlCentre {};
    ControlCentre.AttachTypefaces(&Typography);
    ControlCentre.AccessDraftAppearance().Theme = Frontier::ThemePreset::Dark;
    ControlCentre.ApplyDraftAppearanceImmediate();
    ControlCentre.AccessNotificationStream().ClearPending();

    Frontier::ControlAppearance::ConfigureTheme(ControlCentre.QueryThemeRepository());

    Frontier::Editor::Explorer::ContentBrowser Browser {};
    Browser.AttachThemeRepository(&ControlCentre.QueryThemeRepository());
    Browser.AccessPanel().ConfigureWorkspaceDomain(Frontier::WorkspaceCategory::GeometryCad);
    Browser.AccessPanel().ConfigureShelfExtent(Frontier::ContentShelfExtent::Closed, true);

    Frontier::Components::HeadlessWorkspaceDockContainer Dock {};
    (void)Dock.Initialize(
        Frontier::Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow("SolidArc", SurfaceW, SurfaceH));
    Frontier::Components::ConfigureImGuiWorkspaceStyle(Dock.ActiveTabMetrics());
    Frontier::Components::ConfigureImGuiWorkspaceTheme(Dock.ActiveTabMetrics(), ControlCentre.QueryThemeRepository());

    Frontier::Workspaces::SketcherWorkspaceHost SketcherHost {};
    SketcherHost.AttachContentBrowserPanel(&Browser.AccessPanel());
    std::string HostDiag {};
    if (!SketcherHost.Initialize(
            &DeviceInterchange,
            CompiledShaderDir,
            RepositoryRootPath / "EngineContent/Icons",
            StartupImportPath.empty() ? std::string{} : StartupImportPath.string(),
            &HostDiag))
    {
        std::fprintf(stderr, "[SolidArc] SketcherWorkspaceHost initialization failed: %s\n", HostDiag.c_str());
        Dock.Finalize();
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }
    SketcherHost.RegisterDockPanes(Dock);

    Frontier::GeometricRaster::ImGuiOverlayRasterization Presenter {};
    if (!Presenter.InitializeOverlay(&DeviceInterchange, CompiledShaderDir, nullptr, SurfaceW, SurfaceH, &VkDiag))
    {
        std::fprintf(stderr, "[SolidArc] ImGuiOverlayRasterization initialization failed: %s\n", VkDiag.c_str());
        SketcherHost.Finalize();
        Dock.Finalize();
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }

    const int ListenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (ListenFd < 0)
    {
        std::fprintf(stderr, "[SolidArc] Failed to open TCP socket.\n");
        return 1;
    }
    int ReuseAddr = 1;
    ::setsockopt(ListenFd, SOL_SOCKET, SO_REUSEADDR, &ReuseAddr, sizeof(ReuseAddr));

    sockaddr_in BindAddr {};
    BindAddr.sin_family      = AF_INET;
    BindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    BindAddr.sin_port        = htons(ListenPort);
    if (::bind(ListenFd, reinterpret_cast<sockaddr*>(&BindAddr), sizeof(BindAddr)) < 0 ||
        ::listen(ListenFd, 16) < 0)
    {
        std::fprintf(stderr, "[SolidArc] Failed to bind/listen on 0.0.0.0:%u\n", static_cast<unsigned>(ListenPort));
        ::close(ListenFd);
        return 1;
    }

    std::fprintf(stdout, "[SolidArc] Interactive standalone server listening on http://0.0.0.0:%u\n", static_cast<unsigned>(ListenPort));
    std::fflush(stdout);

    const std::filesystem::path UploadInbox = "/tmp/SolidArcSessionUploads";
    std::error_code FsErr {};
    std::filesystem::create_directories(UploadInbox, FsErr);

    std::vector<StreamClientConnection> Clients {};
    std::vector<QueuedInputEvent>       ImmediateQueue {};
    std::vector<QueuedInputEvent>       DeferredQueue {};
    std::vector<std::string>            PendingDropFiles {};
    std::vector<uint8_t>                CapturedRgba {};
    std::vector<uint8_t>                LatestJpeg {};

    uint32_t SettleTicksRemaining = 6u;
    auto     PreviousTick         = std::chrono::steady_clock::now();

    auto RenderOneTick = [&]()
    {
        const auto NowTick = std::chrono::steady_clock::now();
        const double RawDt = std::chrono::duration<double>(NowTick - PreviousTick).count();
        PreviousTick       = NowTick;
        const double Dt    = std::clamp(RawDt, 1.0 / 240.0, 0.05);

        if (auto* OperatingSurface = Dock.AccessOperatingSurface(Frontier::Components::HostWindowToken{ 0u }))
        {
            OperatingSurface->ResizeSurface(SurfaceW, SurfaceH, 1.0f);
        }

        for (const QueuedInputEvent& Ev : ImmediateQueue)
        {
            switch (Ev.Category)
            {
            case QueuedInputCategory::PointerMove:
                Io.AddMousePosEvent(Ev.AxisX, Ev.AxisY);
                break;
            case QueuedInputCategory::PointerButton:
                if (Ev.Code >= 0 && Ev.Code < 5)
                {
                    Io.AddMouseButtonEvent(Ev.Code, Ev.Pressed);
                }
                break;
            case QueuedInputCategory::WheelScroll:
                Io.AddMouseWheelEvent(Ev.AxisX, Ev.AxisY);
                break;
            case QueuedInputCategory::KeyTransition:
                Io.AddKeyEvent(ImGuiMod_Ctrl, Ev.CtrlMod);
                Io.AddKeyEvent(ImGuiMod_Shift, Ev.ShiftMod);
                Io.AddKeyEvent(ImGuiMod_Alt, Ev.AltMod);
                if (Ev.Code != ImGuiKey_None)
                {
                    Io.AddKeyEvent(static_cast<ImGuiKey>(Ev.Code), Ev.Pressed);
                }
                break;
            case QueuedInputCategory::TextCharacter:
                if (Ev.Code > 0)
                {
                    Io.AddInputCharacter(static_cast<unsigned int>(Ev.Code));
                }
                break;
            case QueuedInputCategory::ReleaseAll:
                for (int Btn = 0; Btn < 5; ++Btn)
                {
                    Io.AddMouseButtonEvent(Btn, false);
                }
                Io.AddKeyEvent(ImGuiMod_Ctrl, false);
                Io.AddKeyEvent(ImGuiMod_Shift, false);
                Io.AddKeyEvent(ImGuiMod_Alt, false);
                break;
            }
        }
        ImmediateQueue.clear();
        ImmediateQueue.swap(DeferredQueue);

        for (const std::string& DropPath : PendingDropFiles)
        {
            std::string DropDiag {};
            (void)SketcherHost.IntakeExternalFileDrop(DropPath, &DropDiag);
        }
        PendingDropFiles.clear();

        Io.DisplaySize = ImVec2(static_cast<float>(SurfaceW), static_cast<float>(SurfaceH));
        Io.DeltaTime   = static_cast<float>(Dt);

        Presenter.PrepareImGuiFrame();
        ImGui::NewFrame();

        const Frontier::Rectangle2D FullBounds { 0.0f, 0.0f, static_cast<float>(SurfaceW), static_cast<float>(SurfaceH) };
        const float HostUiScale = std::clamp(ControlCentre.QueryInterfaceScaleFactor(), 0.75f, 2.0f);
        Frontier::ControlAppearance::ConfigureTheme(ControlCentre.QueryThemeRepository());
        SketcherHost.SynchronizeHostScale(HostUiScale);
        Frontier::Components::ConfigureImGuiWorkspaceTheme(Dock.ActiveTabMetrics(), ControlCentre.QueryThemeRepository());

        auto& BrowserPanel = Browser.AccessPanel();
        BrowserPanel.SynchronizeHostBounds(FullBounds, HostUiScale);
        ControlCentre.SynchronizeHostBounds(FullBounds, HostUiScale);
        if (ControlCentre.QueryFullScreenBlocking() || BrowserPanel.QueryShelfDragging())
        {
            Frontier::WorkspaceAppearance::AssignOverlayPointerShield(FullBounds);
        }
        else
        {
            Frontier::WorkspaceAppearance::AssignOverlayPointerShield(
                BrowserPanel.QueryDrawerPhysicalBounds(),
                BrowserPanel.QueryNotchPhysicalBounds());
            Frontier::WorkspaceAppearance::AppendOverlayPointerShield(ControlCentre.QueryShadePhysicalBounds());
            Frontier::WorkspaceAppearance::AppendOverlayPointerShield(ControlCentre.QueryTabPhysicalBounds());
        }

        SketcherHost.BeginFrame(Dt);
        if (SketcherHost.QueryViewportMaximized())
        {
            SketcherHost.RenderFullscreenViewport(FullBounds.Width, FullBounds.Height);
        }
        else
        {
            Dock.PresentWindowImGuiPanes(Frontier::Components::HostWindowToken{ 0u });
        }
        SketcherHost.EndFrame(Dt, true, true);
        Frontier::WorkspaceAppearance::ClearOverlayPointerShield();
        Browser.Render(FullBounds, Dt, HostUiScale);
        ControlCentre.Render(FullBounds, Dt, static_cast<float>(1.0 / Dt));

        ImGui::Render();
        if (Presenter.PresentImGuiBatches(ImGui::GetDrawData(), SurfaceW, SurfaceH))
        {
            uint32_t CapW = 0u;
            uint32_t CapH = 0u;
            if (Presenter.ReadbackLastPresentedRgba(CapturedRgba, CapW, CapH) && CapW > 0u && CapH > 0u)
            {
                LatestJpeg.clear();
                LatestJpeg.reserve(262144u);
                stbi_write_jpg_to_func(
                    AppendJpegBytesCallback,
                    &LatestJpeg,
                    static_cast<int>(CapW),
                    static_cast<int>(CapH),
                    4,
                    CapturedRgba.data(),
                    86);
            }
        }
    };

    // Warm up 3 initial layout & docking ticks so first connected client gets a fully settled SolidArc viewport
    for (int Warm = 0; Warm < 3; ++Warm)
    {
        RenderOneTick();
    }

    while (true)
    {
        const bool ActiveWorkPending = (SettleTicksRemaining > 0u) || !ImmediateQueue.empty() || !DeferredQueue.empty() || !PendingDropFiles.empty();
        const int PollTimeoutMs = ActiveWorkPending ? 4 : 30;

        std::vector<pollfd> PollDescriptors {};
        PollDescriptors.reserve(1u + Clients.size());
        PollDescriptors.push_back(pollfd{ ListenFd, POLLIN, 0 });
        for (const auto& Client : Clients)
        {
            PollDescriptors.push_back(pollfd{ Client.SocketFd, POLLIN, 0 });
        }

        (void)::poll(PollDescriptors.data(), static_cast<nfds_t>(PollDescriptors.size()), PollTimeoutMs);

        if ((PollDescriptors[0u].revents & POLLIN) != 0)
        {
            sockaddr_in PeerAddr {};
            socklen_t   PeerLen = sizeof(PeerAddr);
            const int ClientFd = ::accept(ListenFd, reinterpret_cast<sockaddr*>(&PeerAddr), &PeerLen);
            if (ClientFd >= 0)
            {
                int NoDelay = 1;
                ::setsockopt(ClientFd, IPPROTO_TCP, TCP_NODELAY, &NoDelay, sizeof(NoDelay));
                Clients.push_back(StreamClientConnection{ ClientFd, false, 0u, false, {} });
            }
        }

        std::array<bool, 5u> ButtonPressedInDrain {};
        std::vector<int>     KeysPressedInDrain {};

        auto ParseCommandLine = [&](StreamClientConnection& Conn, std::string_view Line)
        {
            if (Line == "A")
            {
                if (Conn.InFlightCount > 0u)
                {
                    --Conn.InFlightCount;
                }
                return;
            }
            if (Line == "X")
            {
                ImmediateQueue.push_back(QueuedInputEvent{ QueuedInputCategory::ReleaseAll });
                SettleTicksRemaining = std::max(SettleTicksRemaining, 3u);
                return;
            }
            if (Line.starts_with("M "))
            {
                float Mx = 0.0f;
                float My = 0.0f;
                if (std::sscanf(std::string(Line.substr(2u)).c_str(), "%f %f", &Mx, &My) == 2)
                {
                    ImmediateQueue.push_back(QueuedInputEvent{ QueuedInputCategory::PointerMove, Mx, My });
                    SettleTicksRemaining = std::max(SettleTicksRemaining, 2u);
                }
                return;
            }
            if (Line.starts_with("B "))
            {
                int Btn = 0;
                int Down = 0;
                if (std::sscanf(std::string(Line.substr(2u)).c_str(), "%d %d", &Btn, &Down) == 2 && Btn >= 0 && Btn < 5)
                {
                    QueuedInputEvent Ev {};
                    Ev.Category = QueuedInputCategory::PointerButton;
                    Ev.Code     = Btn;
                    Ev.Pressed  = (Down != 0);
                    if (Ev.Pressed)
                    {
                        ButtonPressedInDrain[static_cast<std::size_t>(Btn)] = true;
                        ImmediateQueue.push_back(Ev);
                    }
                    else if (ButtonPressedInDrain[static_cast<std::size_t>(Btn)])
                    {
                        DeferredQueue.push_back(Ev);
                    }
                    else
                    {
                        ImmediateQueue.push_back(Ev);
                    }
                    SettleTicksRemaining = std::max(SettleTicksRemaining, 4u);
                }
                return;
            }
            if (Line.starts_with("W "))
            {
                float Wx = 0.0f;
                float Wy = 0.0f;
                if (std::sscanf(std::string(Line.substr(2u)).c_str(), "%f %f", &Wx, &Wy) == 2)
                {
                    ImmediateQueue.push_back(QueuedInputEvent{ QueuedInputCategory::WheelScroll, Wx, Wy });
                    SettleTicksRemaining = std::max(SettleTicksRemaining, 3u);
                }
                return;
            }
            if (Line.starts_with("K "))
            {
                char CodeBuf[48] {};
                int Down = 0;
                int Ctrl = 0;
                int Shift = 0;
                int Alt = 0;
                if (std::sscanf(std::string(Line.substr(2u)).c_str(), "%47s %d %d %d %d", CodeBuf, &Down, &Ctrl, &Shift, &Alt) == 5)
                {
                    const ImGuiKey Mapped = BrowserCodeToImGuiKey(CodeBuf);
                    QueuedInputEvent Ev {};
                    Ev.Category = QueuedInputCategory::KeyTransition;
                    Ev.Code     = static_cast<int>(Mapped);
                    Ev.Pressed  = (Down != 0);
                    Ev.CtrlMod  = (Ctrl != 0);
                    Ev.ShiftMod = (Shift != 0);
                    Ev.AltMod   = (Alt != 0);
                    if (Ev.Pressed)
                    {
                        KeysPressedInDrain.push_back(Ev.Code);
                        ImmediateQueue.push_back(Ev);
                    }
                    else if (std::find(KeysPressedInDrain.begin(), KeysPressedInDrain.end(), Ev.Code) != KeysPressedInDrain.end())
                    {
                        DeferredQueue.push_back(Ev);
                    }
                    else
                    {
                        ImmediateQueue.push_back(Ev);
                    }
                    SettleTicksRemaining = std::max(SettleTicksRemaining, 4u);
                }
                return;
            }
            if (Line.starts_with("C "))
            {
                int CodePt = 0;
                if (std::sscanf(std::string(Line.substr(2u)).c_str(), "%d", &CodePt) == 1 && CodePt > 0)
                {
                    QueuedInputEvent Ev {};
                    Ev.Category = QueuedInputCategory::TextCharacter;
                    Ev.Code     = CodePt;
                    ImmediateQueue.push_back(Ev);
                    SettleTicksRemaining = std::max(SettleTicksRemaining, 3u);
                }
                return;
            }
            if (Line.starts_with("R "))
            {
                unsigned NewW = 0u;
                unsigned NewH = 0u;
                if (std::sscanf(std::string(Line.substr(2u)).c_str(), "%u %u", &NewW, &NewH) == 2)
                {
                    NewW = std::clamp(NewW, 800u, 1920u) & ~1u;
                    NewH = std::clamp(NewH, 540u, 1160u) & ~1u;
                    if (NewW != SurfaceW || NewH != SurfaceH)
                    {
                        SurfaceW = NewW;
                        SurfaceH = NewH;
                        SettleTicksRemaining = std::max(SettleTicksRemaining, 4u);
                    }
                    else
                    {
                        Conn.NeedsInitial = true;
                    }
                }
                return;
            }
        };

        for (std::size_t CIdx = 0u; CIdx < Clients.size();)
        {
            bool DisconnectClient = false;
            const std::size_t PollIdx = 1u + CIdx;
            if (PollIdx < PollDescriptors.size() && (PollDescriptors[PollIdx].revents & (POLLIN | POLLERR | POLLHUP)) != 0)
            {
                std::array<uint8_t, 65536u> Chunk {};
                const ssize_t Count = ::recv(Clients[CIdx].SocketFd, Chunk.data(), Chunk.size(), 0);
                if (Count <= 0)
                {
                    DisconnectClient = true;
                }
                else
                {
                    auto& Rx = Clients[CIdx].ReceiveBuffer;
                    Rx.insert(Rx.end(), Chunk.begin(), Chunk.begin() + Count);
                }
            }

            auto& Conn = Clients[CIdx];
            if (!DisconnectClient && !Conn.UpgradedWs)
            {
                const std::string_view RawView(reinterpret_cast<const char*>(Conn.ReceiveBuffer.data()), Conn.ReceiveBuffer.size());
                const std::size_t HeaderEnd = RawView.find("\r\n\r\n");
                if (HeaderEnd != std::string_view::npos)
                {
                    const std::string HeaderText(RawView.substr(0u, HeaderEnd));
                    const std::size_t BodyStart = HeaderEnd + 4u;

                    if (HeaderText.starts_with("GET /ws"))
                    {
                        std::string WsKey {};
                        const std::size_t KeyPos = HeaderText.find("Sec-WebSocket-Key:");
                        if (KeyPos != std::string::npos)
                        {
                            const std::size_t LineEnd = HeaderText.find("\r\n", KeyPos);
                            std::string_view Val(HeaderText.data() + KeyPos + 18u, LineEnd - (KeyPos + 18u));
                            while (!Val.empty() && (Val.front() == ' ' || Val.front() == '\t')) Val.remove_prefix(1u);
                            while (!Val.empty() && (Val.back() == ' ' || Val.back() == '\r')) Val.remove_suffix(1u);
                            WsKey.assign(Val);
                        }
                        const std::string AcceptToken = EncodeSixBitDigest(
                            ComputeSha1Digest(WsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
                        const std::string Handshake =
                            "HTTP/1.1 101 Switching Protocols\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            "Sec-WebSocket-Accept: " + AcceptToken + "\r\n\r\n";
                        if (WriteAllBytes(Conn.SocketFd, Handshake.data(), Handshake.size()))
                        {
                            Conn.UpgradedWs   = true;
                            Conn.NeedsInitial = true;
                            Conn.ReceiveBuffer.erase(Conn.ReceiveBuffer.begin(), Conn.ReceiveBuffer.begin() + static_cast<std::ptrdiff_t>(BodyStart));
                        }
                        else
                        {
                            DisconnectClient = true;
                        }
                    }
                    else if (HeaderText.starts_with("POST /upload"))
                    {
                        std::size_t ContentLength = 0u;
                        const std::size_t LenPos = HeaderText.find("Content-Length:");
                        if (LenPos != std::string::npos)
                        {
                            ContentLength = static_cast<std::size_t>(std::strtoull(HeaderText.c_str() + LenPos + 15u, nullptr, 10));
                        }
                        if (Conn.ReceiveBuffer.size() >= BodyStart + ContentLength)
                        {
                            std::string FileName = "ImportedFile.step";
                            const std::size_t NamePos = HeaderText.find("name=");
                            if (NamePos != std::string::npos)
                            {
                                const std::size_t SpaceEnd = HeaderText.find_first_of(" &\r\n", NamePos + 5u);
                                FileName = DecodeUrlComponent(std::string_view(HeaderText).substr(NamePos + 5u, SpaceEnd - (NamePos + 5u)));
                            }
                            FileName = std::filesystem::path(FileName).filename().string();
                            if (FileName.empty()) FileName = "ImportedFile.step";

                            const std::filesystem::path DestPath = UploadInbox / FileName;
                            {
                                std::ofstream Out(DestPath, std::ios::binary | std::ios::trunc);
                                Out.write(reinterpret_cast<const char*>(Conn.ReceiveBuffer.data() + BodyStart), static_cast<std::streamsize>(ContentLength));
                            }

                            std::string DropDiag {};
                            const bool Accepted = SketcherHost.IntakeExternalFileDrop(DestPath.string(), &DropDiag);
                            SettleTicksRemaining = std::max(SettleTicksRemaining, 5u);

                            const std::string BodyMsg = Accepted ? "OK" : (DropDiag.empty() ? "Refused by SolidArc" : DropDiag);
                            const std::string Resp =
                                std::string("HTTP/1.1 ") + (Accepted ? "200 OK\r\n" : "400 Bad Request\r\n") +
                                "Content-Type: text/plain; charset=utf-8\r\n"
                                "Content-Length: " + std::to_string(BodyMsg.size()) + "\r\n"
                                "Connection: close\r\n\r\n" + BodyMsg;
                            (void)WriteAllBytes(Conn.SocketFd, Resp.data(), Resp.size());
                            DisconnectClient = true;
                        }
                    }
                    else if (HeaderText.starts_with("GET /export"))
                    {
                        const bool WantStep = (HeaderText.find("ext=.step") != std::string::npos);
                        const std::string OutName = WantStep ? "SolidArcExport.step" : "SolidArcDocument.solidarc";
                        const std::filesystem::path TempOut = UploadInbox / OutName;
                        std::error_code RemEc {};
                        std::filesystem::remove(TempOut, RemEc);

                        bool ExportOk = false;
                        std::string ErrReason {};
                        if (WantStep)
                        {
                            const auto Outcome = Frontier::Workspaces::Sketcher::ExportSketcherBodies(
                                SketcherHost.AccessState().Document, TempOut.string());
                            ExportOk  = Outcome.Refusal.empty();
                            ErrReason = Outcome.Refusal;
                        }
                        else
                        {
                            ExportOk = Frontier::Workspaces::Sketcher::SaveSketcherDocument(
                                SketcherHost.AccessState().Document, TempOut.string());
                            if (!ExportOk) ErrReason = "Failed to serialize .solidarc container.";
                        }

                        if (ExportOk && std::filesystem::exists(TempOut))
                        {
                            std::ifstream In(TempOut, std::ios::binary);
                            std::vector<char> FileBytes((std::istreambuf_iterator<char>(In)), std::istreambuf_iterator<char>());
                            const std::string RespHdr =
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Type: application/octet-stream\r\n"
                                "Content-Disposition: attachment; filename=\"" + OutName + "\"\r\n"
                                "Content-Length: " + std::to_string(FileBytes.size()) + "\r\n"
                                "Connection: close\r\n\r\n";
                            (void)WriteAllBytes(Conn.SocketFd, RespHdr.data(), RespHdr.size());
                            (void)WriteAllBytes(Conn.SocketFd, FileBytes.data(), FileBytes.size());
                        }
                        else
                        {
                            const std::string Msg = ErrReason.empty() ? "Nothing to export (construct a solid body first)." : ErrReason;
                            const std::string Resp =
                                "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain; charset=utf-8\r\n"
                                "Content-Length: " + std::to_string(Msg.size()) + "\r\nConnection: close\r\n\r\n" + Msg;
                            (void)WriteAllBytes(Conn.SocketFd, Resp.data(), Resp.size());
                        }
                        DisconnectClient = true;
                    }
                    else
                    {
                        const std::size_t HtmlLen = std::strlen(EmbeddedClientHtml);
                        const std::string RespHdr =
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/html; charset=utf-8\r\n"
                            "Cache-Control: no-store\r\n"
                            "Content-Length: " + std::to_string(HtmlLen) + "\r\n"
                            "Connection: close\r\n\r\n";
                        (void)WriteAllBytes(Conn.SocketFd, RespHdr.data(), RespHdr.size());
                        (void)WriteAllBytes(Conn.SocketFd, EmbeddedClientHtml, HtmlLen);
                        DisconnectClient = true;
                    }
                }
            }

            if (!DisconnectClient && Conn.UpgradedWs)
            {
                auto& Rx = Conn.ReceiveBuffer;
                while (Rx.size() >= 2u)
                {
                    const uint8_t Opcode = Rx[0u] & 0x0Fu;
                    const bool    Masked = (Rx[1u] & 0x80u) != 0u;
                    std::size_t   PayLen = Rx[1u] & 0x7Fu;
                    std::size_t   HdrLen = 2u;

                    if (PayLen == 126u)
                    {
                        if (Rx.size() < 4u) break;
                        PayLen = (static_cast<std::size_t>(Rx[2u]) << 8u) | static_cast<std::size_t>(Rx[3u]);
                        HdrLen = 4u;
                    }
                    else if (PayLen == 127u)
                    {
                        if (Rx.size() < 10u) break;
                        PayLen = 0u;
                        for (std::size_t B = 0u; B < 8u; ++B)
                        {
                            PayLen = (PayLen << 8u) | static_cast<std::size_t>(Rx[2u + B]);
                        }
                        HdrLen = 10u;
                    }

                    const std::size_t MaskBytes = Masked ? 4u : 0u;
                    if (Rx.size() < HdrLen + MaskBytes + PayLen)
                    {
                        break;
                    }

                    if (Opcode == 0x8u)
                    {
                        DisconnectClient = true;
                        break;
                    }
                    if (Opcode == 0x9u)
                    {
                        const uint8_t Pong[2] { 0x8Au, 0x00u };
                        (void)WriteAllBytes(Conn.SocketFd, Pong, 2u);
                    }
                    else if (Opcode == 0x1u)
                    {
                        std::string Text(PayLen, '\0');
                        for (std::size_t Idx = 0u; Idx < PayLen; ++Idx)
                        {
                            uint8_t Byte = Rx[HdrLen + MaskBytes + Idx];
                            if (Masked)
                            {
                                Byte ^= Rx[HdrLen + (Idx & 3u)];
                            }
                            Text[Idx] = static_cast<char>(Byte);
                        }
                        ParseCommandLine(Conn, Text);
                    }

                    Rx.erase(Rx.begin(), Rx.begin() + static_cast<std::ptrdiff_t>(HdrLen + MaskBytes + PayLen));
                }
            }

            if (DisconnectClient)
            {
                ::close(Conn.SocketFd);
                Clients.erase(Clients.begin() + static_cast<std::ptrdiff_t>(CIdx));
            }
            else
            {
                ++CIdx;
            }
        }

        const bool NeedTick = (SettleTicksRemaining > 0u) || !ImmediateQueue.empty() || !DeferredQueue.empty() || !PendingDropFiles.empty();
        if (NeedTick)
        {
            RenderOneTick();
            if (SettleTicksRemaining > 0u)
            {
                --SettleTicksRemaining;
            }
            if (!LatestJpeg.empty())
            {
                for (std::size_t CIdx = 0u; CIdx < Clients.size();)
                {
                    auto& Conn = Clients[CIdx];
                    if (Conn.UpgradedWs && Conn.InFlightCount < 2u)
                    {
                        if (SendWebSocketBinary(Conn.SocketFd, LatestJpeg))
                        {
                            ++Conn.InFlightCount;
                            Conn.NeedsInitial = false;
                            ++CIdx;
                        }
                        else
                        {
                            ::close(Conn.SocketFd);
                            Clients.erase(Clients.begin() + static_cast<std::ptrdiff_t>(CIdx));
                        }
                    }
                    else
                    {
                        ++CIdx;
                    }
                }
            }
        }
        else if (!LatestJpeg.empty())
        {
            for (std::size_t CIdx = 0u; CIdx < Clients.size();)
            {
                auto& Conn = Clients[CIdx];
                if (Conn.UpgradedWs && Conn.NeedsInitial && Conn.InFlightCount < 2u)
                {
                    if (SendWebSocketBinary(Conn.SocketFd, LatestJpeg))
                    {
                        ++Conn.InFlightCount;
                        Conn.NeedsInitial = false;
                        ++CIdx;
                    }
                    else
                    {
                        ::close(Conn.SocketFd);
                        Clients.erase(Clients.begin() + static_cast<std::ptrdiff_t>(CIdx));
                    }
                }
                else
                {
                    ++CIdx;
                }
            }
        }
    }
}

} // namespace
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int Argc, char** Argv)
{
    const std::span<const char* const> Args(
        const_cast<const char* const*>(Argv),
        static_cast<std::size_t>(Argc > 0 ? Argc : 0));

    const auto RuntimePaths = Frontier::Lifecycle::ResolveDesktopResourcePaths(
        Argc > 0 && Argv != nullptr && Argv[0] != nullptr ? std::filesystem::path(Argv[0]) : std::filesystem::path{},
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR),
        std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR));

#if defined(__unix__) || defined(__APPLE__)
    uint16_t              ServePort = 0u;
    std::filesystem::path StartupImport {};
    for (const char* RawArg : Args)
    {
        if (RawArg == nullptr) continue;
        const std::string_view Arg(RawArg);
        if (Arg.starts_with("--serve="))
        {
            ServePort = static_cast<uint16_t>(std::clamp(std::atoi(std::string(Arg.substr(8u)).c_str()), 1, 65535));
        }
        else if (Arg.starts_with("--import="))
        {
            StartupImport = std::filesystem::path(Arg.substr(9u));
        }
    }
    if (ServePort != 0u)
    {
        return RunInteractiveStreamServer(
            ServePort,
            RuntimePaths.RepositoryRootPath,
            RuntimePaths.CompiledShaderDirectory,
            StartupImport);
    }
#endif

    Frontier::Lifecycle::DesktopLaunchOptions Options {};
    Options.DefaultLaunchMode          = Frontier::Configuration::HostLaunchMode::StandaloneWorkspace;
    Options.InitialWorkspace           = Frontier::WorkspaceCategory::GeometryCad;
    Options.IsolateStandaloneWorkspace = true;
    Options.WindowTitle                = "SolidArc";
    Options.InitialWidthPx             = 1600u;
    Options.InitialHeightPx            = 980u;
    Options.OpenContentBrowser         = false;

    return Frontier::Lifecycle::LaunchDesktopWorkspace(
        Options,
        Args,
        RuntimePaths.RepositoryRootPath,
        RuntimePaths.CompiledShaderDirectory);
}
