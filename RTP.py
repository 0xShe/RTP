import os
import random
import shutil
import string
import subprocess
import sys
import threading
import tkinter as tk
import webbrowser
from tkinter import ttk, scrolledtext, messagebox, filedialog

GITHUB_URL = "https://github.com/0xShe/RTP"

def resolve_project_root():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        script_dir,
        os.path.dirname(script_dir),
    ]

    for candidate in candidates:
        include_dir = os.path.join(candidate, "include")
        src_dir = os.path.join(candidate, "src")
        if os.path.isdir(include_dir) and os.path.isdir(src_dir):
            return candidate

    return script_dir

def resolve_bundled_zig(project_root):
    zig_name = "zig.exe" if os.name == 'nt' else "zig"
    candidates = [
        os.path.join(project_root, zig_name),
        os.path.join(project_root, "zig", zig_name),
        os.path.join(os.path.dirname(os.path.abspath(__file__)), zig_name),
    ]

    for candidate in candidates:
        if os.path.exists(candidate):
            return candidate

    return ""

def resolve_tool_near_binary(binary_path, tool_names):
    if not binary_path:
        return ""

    binary_dir = os.path.dirname(os.path.abspath(binary_path))
    for tool_name in tool_names:
        candidate = os.path.join(binary_dir, tool_name)
        if os.path.exists(candidate):
            return candidate

    return ""

def resolve_llvm_lipo(project_root, compiler_path=""):
    tool_names = [
        "llvm-lipo.exe",
        "llvm-lipo",
        "lipo.exe",
        "lipo",
    ]

    if compiler_path:
        nearby = resolve_tool_near_binary(compiler_path, tool_names)
        if nearby:
            return nearby

    for tool_name in tool_names:
        in_path = shutil.which(tool_name)
        if in_path:
            return in_path

    for base_dir in [project_root, os.path.join(project_root, "zig")]:
        for tool_name in tool_names:
            candidate = os.path.join(base_dir, tool_name)
            if os.path.exists(candidate):
                return candidate

    return ""

# --- I18N Dictionary ---
LANG = {
    "en": {
        "title": "RTP (Red Team Proxy) by：0xShe website：sbbbb.cn",
        "title_label": "RTP",
        "subtitle_label": "A lightweight tunneling and proxy tool built for red team engagements.",
        "github_label": "GitHub: {0}",
        "lang_toggle": "切换至中文",
        "target_os": " Target Platform ",
        "os_win": "Windows (exe)",
        "os_lin": "Linux (ELF)",
        "os_mac": "macOS (Mach-O)",
        "target_arch": " Target Architecture & Version ",
        "arch_win_modern": "Modern (Win10/Win11/Server 2016+)",
        "arch_win_legacy": "Legacy (Win7/Server 2003/2008)",
        "arch_lin_x64": "x86_64 (amd64 - Default)",
        "arch_lin_arm64": "aarch64 (ARM64)",
        "arch_lin_x86": "i686 (x86 32-bit)",
        "arch_mac_x64": "x86_64 (Intel Mac)",
        "arch_mac_arm64": "aarch64 (Apple Silicon)",
        "arch_mac_univ": "Universal (x86_64 + ARM64)",
        "adv_settings": " Advanced Settings ",
        "custom_compiler": "Custom Compiler Path (Optional):",
        "browse_btn": "Browse...",
        "build_btn": "🚀 Generate Mutated Payload",
        "log_init": "RTP Builder Initialized. Ready to compile.",
        "log_start": "[*] Starting Evasion Build Process...",
        "log_mutating": "[*] Generating dynamic mutation signatures...",
        "log_injecting": "[*] Injecting mutation headers...",
        "log_compiling": "[*] Compiling for {0} - {1}...",
        "log_warn_linux": "[!] WARNING: Native Windows cannot directly use plain 'gcc' to build Linux payloads. Use Zig or a Linux cross-compiler.",
        "log_warn_mac": "[!] WARNING: Non-macOS hosts need osxcross or Zig to build Mach-O payloads.",
        "log_mac_slice": "[*] Building macOS slice: {0} ({1})...",
        "log_mac_merge": "[*] Merging macOS slices with {0}...",
        "log_mac_split_only": "[!] WARNING: lipo/llvm-lipo not found. Generated separate x86_64 and ARM64 Mach-O files instead of a single Universal binary.",
        "log_warn_cc": "[!] WARNING: Cross-compiler '{}' not found in PATH. Build may fail.\n\n[Solution]\n1. [Recommended] Use Zig: Download zig, extract, and select zig.exe as custom compiler.\n   ⚠️NOTE: Do not copy zig.exe out of its folder. It needs the adjacent 'lib' directory!\n2. Windows users: Run this in WSL (Ubuntu) or install MinGW-w64 cross-toolchain.\n3. Linux users: Run 'sudo apt install gcc-mingw-w64' (for Win) or 'sudo apt install gcc' (for Linux).\n4. Or specify the absolute compiler path above.",
        "log_err_cc_path": "[-] Error: Custom compiler path '{0}' does not exist.",
        "log_success": "[+] SUCCESS! Payload generated at: {0}",
        "log_fail": "[-] BUILD FAILED.",
        "msg_success": "Mutated payload compiled successfully:\n{0}",
        "msg_fail": "Compilation failed. Check logs.",
        "cmd_frame": " Command Generator ",
        "payload_frame": " Payload Builder ",
        "log_frame": " Build Logs ",
        "cmd_mode": "Scenario:",
        "cmd_note": "Live command preview. '@' marks RTP tunnel endpoints.",
        "cmd_server_ip": "Server IP:",
        "cmd_tunnel_port": "Tunnel Port:",
        "cmd_access_port": "Access Port:",
        "cmd_local_port": "Local Listen Port:",
        "cmd_second_port": "Second Listen Port:",
        "cmd_target_host": "Target Host/IP:",
        "cmd_target_port": "Target Port:",
        "cmd_service_port": "Service Port:",
        "cmd_key": "Key:",
        "cmd_user": "SOCKS5 User:",
        "cmd_pass": "SOCKS5 Pass:",
        "cmd_tls": "Enable TLS",
        "cmd_reconnect": "Enable Reconnect",
        "cmd_quiet": "Quiet Mode",
        "cmd_auth": "Enable SOCKS5 Auth",
        "cmd_tunnel": "Use Tunnel Endpoints (@)",
        "cmd_preview": " Generated Commands ",
        "cmd_copy": "Copy Commands",
        "cmd_copy_done": "Commands copied to clipboard.",
        "cmd_mode_tcp_forward": "TCP Forward (L-R)",
        "cmd_mode_tcp_bridge": "TCP Bridge (L-L)",
        "cmd_mode_socks5": "SOCKS5 Listener",
        "cmd_mode_reverse_socks": "Reverse SOCKS5 Tunnel",
        "cmd_mode_reverse_service": "Reverse Service Tunnel",
        "cmd_mode_udp": "UDP Forward",
        "cmd_role_server": "# Server",
        "cmd_role_target": "# Target",
        "cmd_role_operator": "# Operator",
        "cmd_operator_hint": "# Connect your client to {0}:{1}",
        "cmd_operator_socks_hint": "# Use {0}:{1} as the SOCKS5 endpoint",
        "cmd_operator_service_hint": "# Access the exposed service via {0}:{1}"
    },
    "zh": {
        "title": "RTP (Red Team Proxy) 内网穿透免杀器 作者：0xShe 官网：sbbbb.cn",
        "title_label": "RTP 免杀隧道代理",
        "subtitle_label": "一个为红队实战设计的轻量级隧道与代理工具。",
        "github_label": "GitHub: {0}",
        "lang_toggle": "Switch to English",
        "target_os": " 目标平台 (Target Platform) ",
        "os_win": "Windows (exe)",
        "os_lin": "Linux (ELF)",
        "os_mac": "macOS (Mach-O)",
        "target_arch": " 架构与版本 (Architecture & Version) ",
        "arch_win_modern": "现代版 (Win10/Win11/Server 2016+)",
        "arch_win_legacy": "旧版兼容 (Win7/Server 2003/2008)",
        "arch_lin_x64": "x86_64 (amd64 - 默认)",
        "arch_lin_arm64": "aarch64 (ARM64)",
        "arch_lin_x86": "i686 (x86 32位)",
        "arch_mac_x64": "x86_64 (Intel Mac)",
        "arch_mac_arm64": "aarch64 (Apple Silicon)",
        "arch_mac_univ": "通用架构 (x86_64 + ARM64)",
        "adv_settings": " 高级设置 (Advanced Settings) ",
        "custom_compiler": "自定义编译器路径 (留空使用默认):",
        "browse_btn": "浏览...",
        "build_btn": "🚀 生成免杀载荷 (Build Payload)",
        "log_init": "RTP 构建器已初始化。等待编译...",
        "log_start": "[*] 开始变异免杀编译流程...",
        "log_mutating": "[*] 正在生成动态变异混淆签名...",
        "log_injecting": "[*] 正在注入变异头文件...",
        "log_compiling": "[*] 正在编译 {0} - {1}...",
        "log_warn_linux": "[!] 警告: Windows 原生环境不能直接用普通 gcc 产出 Linux 载荷，请改用 Zig 或安装对应交叉编译器。",
        "log_warn_mac": "[!] 警告: 非 macOS 主机若要编译 Mach-O，请使用 osxcross 或 Zig。",
        "log_mac_slice": "[*] 正在构建 macOS 切片: {0} ({1})...",
        "log_mac_merge": "[*] 正在使用 {0} 合并 macOS 切片...",
        "log_mac_split_only": "[!] 警告: 未找到 lipo/llvm-lipo，已分别生成 x86_64 与 ARM64 Mach-O 文件，未合并为单个 Universal 二进制。",
        "log_warn_cc": "[!] 警告: 未在 PATH 中找到交叉编译器 '{}'。编译可能失败。\n\n【解决方案】\n1. 【首选推荐】使用 Zig：下载 Zig (https://ziglang.org)，解压后在上方选择 zig.exe。\n   ⚠️注意: 必须保持 Zig 文件夹完整，不可单独拷出 zig.exe (依赖同级 lib 目录)！\n2. Windows用户: 请下载 SysProgs 提供的跨平台编译链或使用 WSL。\n3. Linux用户: 运行 'sudo apt install gcc-mingw-w64' 或 'gcc'。\n4. 或者直接指定已有的绝对编译器路径。",
        "log_err_cc_path": "[-] 错误: 找不到自定义编译器 '{0}'。",
        "log_success": "[+] 成功！免杀载荷已生成至: {0}",
        "log_fail": "[-] 编译失败！",
        "msg_success": "免杀载荷编译成功:\n{0}",
        "msg_fail": "编译失败，请检查下方日志。",
        "cmd_frame": " 命令生成器 (Command Generator) ",
        "payload_frame": " 生成载荷 (Payload Builder) ",
        "log_frame": " 编译日志 (Build Logs) ",
        "cmd_mode": "功能场景:",
        "cmd_note": "根据当前功能实时生成命令。'@' 表示 RTP 隧道端点。",
        "cmd_server_ip": "服务器IP:",
        "cmd_tunnel_port": "隧道端口:",
        "cmd_access_port": "访问端口:",
        "cmd_local_port": "本地监听端口:",
        "cmd_second_port": "第二监听端口:",
        "cmd_target_host": "目标主机/IP:",
        "cmd_target_port": "目标端口:",
        "cmd_service_port": "服务端口:",
        "cmd_key": "密钥:",
        "cmd_user": "SOCKS5 用户:",
        "cmd_pass": "SOCKS5 密码:",
        "cmd_tls": "启用 TLS 伪装",
        "cmd_reconnect": "启用自动重连",
        "cmd_quiet": "静默模式",
        "cmd_auth": "启用 SOCKS5 认证",
        "cmd_tunnel": "使用隧道端点 (@)",
        "cmd_preview": " 生成命令预览 ",
        "cmd_copy": "一键复制命令",
        "cmd_copy_done": "命令已复制到剪贴板。",
        "cmd_mode_tcp_forward": "TCP 正向转发 (L-R)",
        "cmd_mode_tcp_bridge": "TCP 双监听桥接 (L-L)",
        "cmd_mode_socks5": "SOCKS5 监听入口",
        "cmd_mode_reverse_socks": "反连 SOCKS5 隧道",
        "cmd_mode_reverse_service": "反连服务暴露隧道",
        "cmd_mode_udp": "UDP 转发",
        "cmd_role_server": "# 服务器",
        "cmd_role_target": "# 靶机",
        "cmd_role_operator": "# 攻击机",
        "cmd_operator_hint": "# 让客户端连接到 {0}:{1}",
        "cmd_operator_socks_hint": "# 将 {0}:{1} 作为 SOCKS5 入口使用",
        "cmd_operator_service_hint": "# 通过 {0}:{1} 访问暴露出来的服务"
    }
}

def generate_random_string(length=12):
    return ''.join(random.choices(string.ascii_letters, k=length))

def generate_obfuscation_header(filepath):
    functions_to_obfuscate = [
        "rtp_init", "rtp_run", "rtp_cleanup",
        "net_init", "net_cleanup", "net_set_nonblocking", "net_listen", "net_connect",
        "rc4_init", "rc4_crypt",
        "parse_args", "print_usage"
    ]
    
    mapping_logs = []
    with open(filepath, 'w') as f:
        f.write("#ifndef MUTATED_H\n#define MUTATED_H\n\n")
        f.write("// Auto-generated by RTP GUI Builder\n")
        f.write(f"// Timestamp: {random.randint(100000, 999999)}\n\n")
        
        for func in functions_to_obfuscate:
            mutated_name = "rtp_" + generate_random_string()
            f.write(f"#define {func} {mutated_name}\n")
            mapping_logs.append(f"  - {func} -> {mutated_name}")
            
        f.write("\n#endif // MUTATED_H\n")
    return "\n".join(mapping_logs)

def inject_include(target_dir):
    for h_file in ["rtp.h", "net.h", "crypto.h"]:
        path = os.path.join(target_dir, "include", h_file)
        if not os.path.exists(path):
            continue
        with open(path, 'r') as f:
            content = f.read()
        
        if "mutated.h" not in content:
            with open(path, 'w') as f:
                f.write('#include "mutated.h"\n' + content)

class RTPBuilderGUI:
    def __init__(self, root):
        self.root = root
        self.current_lang = "zh"  # Default to Chinese
        self.base_dir = resolve_project_root()
        self.scenario_keys = [
            "tcp_forward",
            "tcp_bridge",
            "socks5",
            "reverse_socks",
            "reverse_service",
            "udp"
        ]
        self.colors = {
            "bg": "#EEF4FB",
            "panel": "#FFFFFF",
            "panel_alt": "#F7FAFE",
            "border": "#D7E3F1",
            "border_strong": "#A9C4E7",
            "text": "#17324D",
            "muted": "#5C7795",
            "accent": "#2F80ED",
            "accent_hover": "#1F6DD6",
            "accent_soft": "#DCEBFF",
            "success": "#1C9A6D",
            "preview_bg": "#F6FAFF"
        }
        
        # --- Theme & UI Setup (Light Tech Style) ---
        self.root.geometry("1500x800")
        self.root.configure(bg=self.colors["bg"])
        
        self.style = ttk.Style()
        self.style.theme_use('clam')
        self.configure_theme()
        self.init_command_generator_vars()
        self.setup_ui()
        self.update_texts()
        self.log(self.get_text("log_init"))

    def get_text(self, key):
        return LANG[self.current_lang][key]

    def configure_theme(self):
        c = self.colors
        self.style.configure("TFrame", background=c["bg"])
        self.style.configure("Card.TFrame", background=c["panel"], relief="flat")
        self.style.configure(
            "Section.TLabelframe",
            background=c["panel"],
            borderwidth=1,
            relief="solid",
            bordercolor=c["border"]
        )
        self.style.configure(
            "Section.TLabelframe.Label",
            background=c["panel"],
            foreground=c["text"],
            font=("Microsoft YaHei", 10, "bold")
        )
        self.style.configure(
            "SubSection.TLabelframe",
            background=c["panel_alt"],
            borderwidth=1,
            relief="solid",
            bordercolor=c["border"]
        )
        self.style.configure(
            "SubSection.TLabelframe.Label",
            background=c["panel_alt"],
            foreground=c["text"],
            font=("Microsoft YaHei", 9, "bold")
        )
        self.style.configure(
            "Choice.TRadiobutton",
            background=c["panel_alt"],
            foreground=c["text"],
            font=("Microsoft YaHei", 9),
            indicatorcolor=c["panel_alt"],
            indicatormargin=4
        )
        self.style.map(
            "Choice.TRadiobutton",
            foreground=[("selected", c["accent"]), ("active", c["accent"])],
            background=[("active", c["panel_alt"])]
        )
        self.style.configure(
            "Toggle.TCheckbutton",
            background=c["panel"],
            foreground=c["text"],
            font=("Microsoft YaHei", 9)
        )
        self.style.map(
            "Toggle.TCheckbutton",
            foreground=[("selected", c["accent"]), ("active", c["accent"])]
        )
        self.style.configure(
            "Primary.TButton",
            font=("Microsoft YaHei", 10, "bold"),
            padding=(10, 8),
            background=c["accent"],
            foreground="white",
            borderwidth=0,
            focusthickness=0
        )
        self.style.map(
            "Primary.TButton",
            background=[("active", c["accent_hover"]), ("disabled", "#B6CBE3")],
            foreground=[("disabled", "#F4F8FD")]
        )
        self.style.configure(
            "Secondary.TButton",
            font=("Microsoft YaHei", 9, "bold"),
            padding=(8, 6),
            background=c["accent_soft"],
            foreground=c["accent"],
            borderwidth=0,
            focusthickness=0
        )
        self.style.map(
            "Secondary.TButton",
            background=[("active", "#CFE3FF"), ("disabled", "#E8EEF5")]
        )
        self.style.configure(
            "Lang.TButton",
            font=("Microsoft YaHei", 9),
            padding=(8, 5),
            background=c["panel"],
            foreground=c["text"],
            borderwidth=1,
            relief="solid",
            bordercolor=c["border"]
        )
        self.style.map("Lang.TButton", background=[("active", c["panel_alt"])])
        self.style.configure(
            "Field.TEntry",
            fieldbackground=c["panel"],
            foreground=c["text"],
            borderwidth=1,
            relief="solid",
            insertcolor=c["accent"],
            padding=6
        )
        self.style.map(
            "Field.TEntry",
            bordercolor=[("focus", c["accent"]), ("!focus", c["border"])]
        )
        self.style.configure(
            "Modern.TCombobox",
            fieldbackground=c["panel"],
            background=c["panel"],
            foreground=c["text"],
            borderwidth=1,
            relief="solid",
            arrowsize=14,
            padding=4
        )
        self.style.map(
            "Modern.TCombobox",
            bordercolor=[("focus", c["accent"]), ("!focus", c["border"])],
            fieldbackground=[("readonly", c["panel"])]
        )

    def toggle_lang(self):
        self.current_lang = "en" if self.current_lang == "zh" else "zh"
        self.update_texts()
        
        # Clear log and re-init with new language
        self.log_area.config(state='normal')
        self.log_area.delete(1.0, tk.END)
        self.log_area.config(state='disabled')
        self.log(self.get_text("log_init"))

    def open_github_link(self, _event=None):
        webbrowser.open(GITHUB_URL)

    def init_command_generator_vars(self):
        self.cmd_mode = tk.StringVar(value="reverse_socks")
        self.cmd_mode_label_var = tk.StringVar()
        self.cmd_server_ip = tk.StringVar(value="127.0.0.1")
        self.cmd_tunnel_port = tk.StringVar(value="9998")
        self.cmd_access_port = tk.StringVar(value="7777")
        self.cmd_local_port = tk.StringVar(value="1080")
        self.cmd_second_port = tk.StringVar(value="7777")
        self.cmd_target_host = tk.StringVar(value="192.168.1.10")
        self.cmd_target_port = tk.StringVar(value="3389")
        self.cmd_service_port = tk.StringVar(value="8080")
        self.cmd_key = tk.StringVar(value="RTP_Secret")
        self.cmd_user = tk.StringVar(value="admin")
        self.cmd_pass = tk.StringVar(value="123456")
        self.cmd_use_tls = tk.BooleanVar(value=False)
        self.cmd_use_reconnect = tk.BooleanVar(value=False)
        self.cmd_use_quiet = tk.BooleanVar(value=False)
        self.cmd_use_auth = tk.BooleanVar(value=False)
        self.cmd_use_tunnel = tk.BooleanVar(value=False)

    def setup_ui(self):
        # Replace the default title-bar icon with a tiny blank icon while keeping the normal window chrome.
        self.blank_icon = tk.PhotoImage(width=1, height=1)
        try:
            self.root.iconphoto(True, self.blank_icon)
        except tk.TclError:
            pass
                
        # Header Frame
        header_frame = tk.Frame(
            self.root,
            bg=self.colors["panel"],
            highlightbackground=self.colors["border"],
            highlightthickness=1,
            bd=0
        )
        header_frame.pack(fill="x", pady=(0, 10), padx=8)

        accent_bar = tk.Frame(header_frame, bg=self.colors["accent"], height=3)
        accent_bar.pack(fill="x", side="top")

        header_inner = tk.Frame(header_frame, bg=self.colors["panel"])
        header_inner.pack(fill="x", padx=16, pady=14)
        
        self.lang_btn = ttk.Button(header_inner, style="Lang.TButton", command=self.toggle_lang)
        self.lang_btn.pack(side="right", padx=5)
        
        title_wrap = tk.Frame(header_inner, bg=self.colors["panel"])
        title_wrap.pack(side="left", fill="x", expand=True)

        self.title_lbl = tk.Label(
            title_wrap,
            font=("Microsoft YaHei", 16, "bold"),
            bg=self.colors["panel"],
            fg=self.colors["text"],
            anchor="center",
            justify="center"
        )
        self.title_lbl.pack(side="top", fill="x")

        self.subtitle_lbl = tk.Label(
            title_wrap,
            font=("Microsoft YaHei", 9),
            bg=self.colors["panel"],
            fg=self.colors["muted"],
            anchor="center",
            justify="center",
            wraplength=760
        )
        self.subtitle_lbl.pack(side="top", fill="x", pady=(3, 0))

        self.github_lbl = tk.Label(
            title_wrap,
            font=("Consolas", 9, "underline"),
            bg=self.colors["panel"],
            fg=self.colors["accent"],
            anchor="center",
            justify="center",
            cursor="hand2"
        )
        self.github_lbl.pack(side="top", fill="x", pady=(6, 0))
        self.github_lbl.bind("<Button-1>", self.open_github_link)

        body_frame = tk.Frame(self.root, bg=self.colors["bg"])
        body_frame.pack(fill="both", expand=True, pady=5, padx=8)

        self.left_panel = tk.Frame(body_frame, bg=self.colors["bg"])
        self.left_panel.pack(side="left", fill="both", expand=True, padx=(0, 5))

        self.right_panel = tk.Frame(body_frame, bg=self.colors["bg"])
        self.right_panel.pack(side="left", fill="both", expand=True, padx=(5, 0))

        self.payload_frame = ttk.LabelFrame(self.left_panel, style="Section.TLabelframe")
        self.payload_frame.pack(fill="x", pady=5, padx=0)

        # OS Selection Frame
        self.os_frame = ttk.LabelFrame(self.payload_frame, style="SubSection.TLabelframe")
        self.os_frame.pack(fill="x", pady=6, padx=8)
        
        self.target_os = tk.StringVar(value="windows")
        self.target_os.trace_add("write", self.update_arch_options)
        
        self.rb_win = ttk.Radiobutton(self.os_frame, style="Choice.TRadiobutton", variable=self.target_os, value="windows")
        self.rb_lin = ttk.Radiobutton(self.os_frame, style="Choice.TRadiobutton", variable=self.target_os, value="linux")
        self.rb_mac = ttk.Radiobutton(self.os_frame, style="Choice.TRadiobutton", variable=self.target_os, value="macos")
        
        self.rb_win.pack(side="left", padx=20, pady=10)
        self.rb_lin.pack(side="left", padx=20, pady=10)
        self.rb_mac.pack(side="left", padx=20, pady=10)
        
        # Architecture Selection Frame
        self.arch_frame = ttk.LabelFrame(self.payload_frame, style="SubSection.TLabelframe")
        self.arch_frame.pack(fill="x", pady=6, padx=8)
        
        self.target_arch = tk.StringVar(value="modern")
        self.rb_arch_1 = ttk.Radiobutton(self.arch_frame, style="Choice.TRadiobutton", variable=self.target_arch, value="modern")
        self.rb_arch_2 = ttk.Radiobutton(self.arch_frame, style="Choice.TRadiobutton", variable=self.target_arch, value="legacy")
        self.rb_arch_3 = ttk.Radiobutton(self.arch_frame, style="Choice.TRadiobutton", variable=self.target_arch, value="x86")
        
        self.rb_arch_1.pack(side="left", padx=10, pady=5)
        self.rb_arch_2.pack(side="left", padx=10, pady=5)
        self.rb_arch_3.pack(side="left", padx=10, pady=5)
        
        # Advanced Settings Frame (Custom Compiler)
        self.adv_frame = ttk.LabelFrame(self.payload_frame, style="SubSection.TLabelframe")
        self.adv_frame.pack(fill="x", pady=6, padx=8)
        
        self.cc_label = tk.Label(self.adv_frame, bg=self.colors["panel_alt"], fg=self.colors["muted"], font=("Microsoft YaHei", 9))
        self.cc_label.pack(side="left", padx=10, pady=10)
        
        self.custom_cc_var = tk.StringVar()
        
        # Auto-detect bundled zig from either project root or the bundled zig/ subdirectory.
        bundled_zig = resolve_bundled_zig(self.base_dir)
        if bundled_zig:
            self.custom_cc_var.set(bundled_zig)
        self.cc_entry = ttk.Entry(self.adv_frame, style="Field.TEntry", textvariable=self.custom_cc_var, width=40)
        self.cc_entry.pack(side="left", padx=5, pady=10, fill="x", expand=True)
        
        self.browse_btn = ttk.Button(self.adv_frame, style="Secondary.TButton", command=self.browse_compiler, width=10)
        self.browse_btn.pack(side="left", padx=10, pady=10)
        
        # Build Button
        self.build_btn = ttk.Button(self.payload_frame, style="Primary.TButton", command=self.start_build_thread)
        self.build_btn.pack(fill="x", pady=(6, 10), padx=8)

        # Command Generator Panel
        self.cmd_frame = ttk.LabelFrame(self.right_panel, style="Section.TLabelframe")
        self.cmd_frame.pack(fill="both", expand=True, pady=5, padx=0)

        note_lbl = tk.Label(self.cmd_frame, bg=self.colors["panel"], fg=self.colors["muted"], font=("Microsoft YaHei", 9), anchor="w", justify="left", wraplength=430)
        note_lbl.pack(fill="x", padx=10, pady=(8, 4))
        self.cmd_note_lbl = note_lbl

        mode_frame = tk.Frame(self.cmd_frame, bg=self.colors["panel"])
        mode_frame.pack(fill="x", padx=10, pady=4)
        self.cmd_mode_label = tk.Label(mode_frame, bg=self.colors["panel"], fg=self.colors["text"], font=("Microsoft YaHei", 9, "bold"))
        self.cmd_mode_label.pack(side="left")
        self.cmd_mode_combo = ttk.Combobox(mode_frame, style="Modern.TCombobox", state="readonly", textvariable=self.cmd_mode_label_var, width=26)
        self.cmd_mode_combo.pack(side="left", padx=(8, 0), fill="x", expand=True)
        self.cmd_mode_combo.bind("<<ComboboxSelected>>", self.on_scenario_selected)

        fields_frame = tk.Frame(self.cmd_frame, bg=self.colors["panel"])
        fields_frame.pack(fill="x", padx=10, pady=4)
        fields_frame.grid_columnconfigure(1, weight=1)
        fields_frame.grid_columnconfigure(3, weight=1)

        self.cmd_labels = {}
        self.cmd_entries = {}

        def add_field(row, col, key, var, width=18, show=None):
            label = tk.Label(fields_frame, bg=self.colors["panel"], fg=self.colors["muted"], font=("Microsoft YaHei", 9), anchor="w")
            label.grid(row=row, column=col * 2, sticky="w", padx=(0, 6), pady=4)
            entry = ttk.Entry(fields_frame, style="Field.TEntry", textvariable=var, width=width, show=show)
            entry.grid(row=row, column=col * 2 + 1, sticky="ew", padx=(0, 10), pady=4)
            self.cmd_labels[key] = label
            self.cmd_entries[key] = entry

        add_field(0, 0, "cmd_server_ip", self.cmd_server_ip)
        add_field(0, 1, "cmd_tunnel_port", self.cmd_tunnel_port, width=14)
        add_field(1, 0, "cmd_access_port", self.cmd_access_port, width=14)
        add_field(1, 1, "cmd_local_port", self.cmd_local_port, width=14)
        add_field(2, 0, "cmd_second_port", self.cmd_second_port, width=14)
        add_field(2, 1, "cmd_service_port", self.cmd_service_port, width=14)
        add_field(3, 0, "cmd_target_host", self.cmd_target_host)
        add_field(3, 1, "cmd_target_port", self.cmd_target_port, width=14)
        add_field(4, 0, "cmd_key", self.cmd_key)
        add_field(4, 1, "cmd_user", self.cmd_user)
        add_field(5, 0, "cmd_pass", self.cmd_pass)

        flags_frame = tk.Frame(self.cmd_frame, bg=self.colors["panel"])
        flags_frame.pack(fill="x", padx=10, pady=(4, 8))
        self.cmd_tls_cb = ttk.Checkbutton(flags_frame, style="Toggle.TCheckbutton", variable=self.cmd_use_tls, command=self.on_tls_toggle)
        self.cmd_tls_cb.pack(side="left", padx=(0, 10))
        self.cmd_reconnect_cb = ttk.Checkbutton(flags_frame, style="Toggle.TCheckbutton", variable=self.cmd_use_reconnect, command=self.render_command_preview)
        self.cmd_reconnect_cb.pack(side="left", padx=(0, 10))
        self.cmd_quiet_cb = ttk.Checkbutton(flags_frame, style="Toggle.TCheckbutton", variable=self.cmd_use_quiet, command=self.render_command_preview)
        self.cmd_quiet_cb.pack(side="left", padx=(0, 10))
        self.cmd_auth_cb = ttk.Checkbutton(flags_frame, style="Toggle.TCheckbutton", variable=self.cmd_use_auth, command=self.on_auth_toggle)
        self.cmd_auth_cb.pack(side="left", padx=(0, 10))
        self.cmd_tunnel_cb = ttk.Checkbutton(flags_frame, style="Toggle.TCheckbutton", variable=self.cmd_use_tunnel, command=self.on_tunnel_toggle)
        self.cmd_tunnel_cb.pack(side="left")

        self.cmd_preview_frame = ttk.LabelFrame(self.cmd_frame, style="SubSection.TLabelframe")
        self.cmd_preview_frame.pack(fill="both", expand=True, padx=10, pady=(0, 10))
        self.cmd_preview = scrolledtext.ScrolledText(
            self.cmd_preview_frame,
            height=14,
            state='disabled',
            bg=self.colors["preview_bg"],
            fg=self.colors["text"],
            font=("Consolas", 10),
            bd=0,
            padx=10,
            pady=10,
            insertbackground=self.colors["accent"],
            relief="flat",
            highlightthickness=1,
            highlightbackground=self.colors["border"]
        )
        self.cmd_preview.pack(fill="both", expand=True)

        self.cmd_copy_btn = ttk.Button(self.cmd_frame, style="Secondary.TButton", command=self.copy_command_preview)
        self.cmd_copy_btn.pack(fill="x", padx=10, pady=(0, 10))

        # Log Area (Tech style: Dark gray with light blue text)
        self.log_frame = ttk.LabelFrame(self.left_panel, style="Section.TLabelframe")
        self.log_frame.pack(fill="both", expand=True, pady=5, padx=0)
        self.log_area = scrolledtext.ScrolledText(
            self.log_frame,
            state='disabled',
            bg=self.colors["preview_bg"],
            fg=self.colors["text"],
            font=("Consolas", 10),
            bd=0,
            padx=10,
            pady=10,
            relief="flat",
            highlightthickness=1,
            highlightbackground=self.colors["border"]
        )
        self.log_area.pack(fill="both", expand=True, padx=8, pady=8)

        self.bind_command_generator_traces()
        self.update_arch_options()

    def bind_command_generator_traces(self):
        tracked_vars = [
            self.cmd_mode, self.cmd_server_ip, self.cmd_tunnel_port, self.cmd_access_port,
            self.cmd_local_port, self.cmd_second_port, self.cmd_target_host, self.cmd_target_port,
            self.cmd_service_port, self.cmd_key, self.cmd_user, self.cmd_pass
        ]
        for var in tracked_vars:
            var.trace_add("write", lambda *_args: self.render_command_preview())

    def browse_compiler(self):
        file_path = filedialog.askopenfilename(
            title="Select Compiler Executable",
            filetypes=[("Executable Files", "*.exe"), ("All Files", "*.*")] if os.name == 'nt' else [("All Files", "*.*")]
        )
        if file_path:
            self.custom_cc_var.set(file_path)

    def update_arch_options(self, *args):
        os_val = self.target_os.get()
        if os_val == "windows":
            self.rb_arch_1.config(text=self.get_text("arch_win_modern"), value="modern")
            self.rb_arch_2.config(text=self.get_text("arch_win_legacy"), value="legacy")
            self.rb_arch_3.pack_forget()
            if self.target_arch.get() not in ["modern", "legacy"]:
                self.target_arch.set("modern")
        elif os_val == "linux":
            self.rb_arch_1.config(text=self.get_text("arch_lin_x64"), value="x86_64")
            self.rb_arch_2.config(text=self.get_text("arch_lin_arm64"), value="aarch64")
            self.rb_arch_3.config(text=self.get_text("arch_lin_x86"), value="i686")
            self.rb_arch_3.pack(side="left", padx=10, pady=5)
            if self.target_arch.get() not in ["x86_64", "aarch64", "i686"]:
                self.target_arch.set("x86_64")
        elif os_val == "macos":
            self.rb_arch_1.config(text=self.get_text("arch_mac_x64"), value="x86_64")
            self.rb_arch_2.config(text=self.get_text("arch_mac_arm64"), value="arm64")
            self.rb_arch_3.config(text=self.get_text("arch_mac_univ"), value="universal")
            self.rb_arch_2.pack(side="left", padx=10, pady=5)
            self.rb_arch_3.pack(side="left", padx=10, pady=5)
            if self.target_arch.get() not in ["x86_64", "arm64", "universal"]:
                self.target_arch.set("x86_64")

    def update_texts(self):
        self.root.title(self.get_text("title"))
        self.title_lbl.config(text=self.get_text("title_label"))
        self.subtitle_lbl.config(text=self.get_text("subtitle_label"))
        self.github_lbl.config(text=self.get_text("github_label").format(GITHUB_URL))
        self.lang_btn.config(text=self.get_text("lang_toggle"))
        self.os_frame.config(text=self.get_text("target_os"))
        self.arch_frame.config(text=self.get_text("target_arch"))
        self.adv_frame.config(text=self.get_text("adv_settings"))
        self.cc_label.config(text=self.get_text("custom_compiler"))
        self.browse_btn.config(text=self.get_text("browse_btn"))
        self.rb_win.config(text=self.get_text("os_win"))
        self.rb_lin.config(text=self.get_text("os_lin"))
        self.rb_mac.config(text=self.get_text("os_mac"))
        self.build_btn.config(text=self.get_text("build_btn"))
        self.payload_frame.config(text=self.get_text("payload_frame"))
        self.log_frame.config(text=self.get_text("log_frame"))
        self.cmd_frame.config(text=self.get_text("cmd_frame"))
        self.cmd_mode_label.config(text=self.get_text("cmd_mode"))
        self.cmd_note_lbl.config(text=self.get_text("cmd_note"))
        self.cmd_preview_frame.config(text=self.get_text("cmd_preview"))
        self.cmd_copy_btn.config(text=self.get_text("cmd_copy"))
        self.cmd_tls_cb.config(text=self.get_text("cmd_tls"))
        self.cmd_reconnect_cb.config(text=self.get_text("cmd_reconnect"))
        self.cmd_quiet_cb.config(text=self.get_text("cmd_quiet"))
        self.cmd_auth_cb.config(text=self.get_text("cmd_auth"))
        self.cmd_tunnel_cb.config(text=self.get_text("cmd_tunnel"))
        for key, label in self.cmd_labels.items():
            label.config(text=self.get_text(key))
        self.refresh_scenario_options()
        self.update_arch_options()
        self.render_command_preview()

    def refresh_scenario_options(self):
        values = [self.get_text(f"cmd_mode_{key}") for key in self.scenario_keys]
        current_index = self.scenario_keys.index(self.cmd_mode.get()) if self.cmd_mode.get() in self.scenario_keys else 0
        self.cmd_mode_combo["values"] = values
        self.cmd_mode_combo.current(current_index)
        self.cmd_mode_label_var.set(values[current_index])

    def on_scenario_selected(self, _event=None):
        current_index = self.cmd_mode_combo.current()
        if 0 <= current_index < len(self.scenario_keys):
            self.cmd_mode.set(self.scenario_keys[current_index])
        self.render_command_preview()

    def on_tunnel_toggle(self):
        if not self.cmd_use_tunnel.get() and self.cmd_use_tls.get():
            self.cmd_use_tls.set(False)
        self.render_command_preview()

    def on_tls_toggle(self):
        if self.cmd_use_tls.get() and not self.cmd_use_tunnel.get():
            self.cmd_use_tunnel.set(True)
        self.render_command_preview()

    def on_auth_toggle(self):
        self.render_command_preview()

    def set_label_enabled(self, key, enabled):
        label = self.cmd_labels[key]
        label.config(fg=self.colors["muted"] if enabled else self.colors["border_strong"])

    def set_entry_enabled(self, key, enabled):
        entry = self.cmd_entries[key]
        if enabled:
            entry.state(["!disabled"])
        else:
            entry.state(["disabled"])
        self.set_label_enabled(key, enabled)

    def set_check_enabled(self, widget, enabled, var=None):
        if enabled:
            widget.state(["!disabled"])
        else:
            widget.state(["disabled"])
            if var is not None:
                var.set(False)

    def update_command_controls(self):
        mode = self.cmd_mode.get()
        use_tunnel = self.cmd_use_tunnel.get()
        use_auth = self.cmd_use_auth.get()

        field_matrix = {
            "tcp_forward": {"cmd_local_port", "cmd_target_host", "cmd_target_port", "cmd_key"},
            "tcp_bridge": {"cmd_local_port", "cmd_second_port", "cmd_key"},
            "socks5": {"cmd_local_port", "cmd_key", "cmd_user", "cmd_pass"},
            "reverse_socks": {"cmd_server_ip", "cmd_tunnel_port", "cmd_access_port", "cmd_key", "cmd_user", "cmd_pass"},
            "reverse_service": {"cmd_server_ip", "cmd_tunnel_port", "cmd_access_port", "cmd_service_port", "cmd_key"},
            "udp": {"cmd_local_port", "cmd_target_host", "cmd_target_port", "cmd_key"},
        }
        enabled_fields = field_matrix.get(mode, set()).copy()

        for key in self.cmd_entries:
            self.set_entry_enabled(key, key in enabled_fields)

        allow_tunnel = mode in {"tcp_forward", "tcp_bridge", "socks5", "reverse_socks", "reverse_service", "udp"}
        allow_reconnect = mode in {"reverse_socks"}
        allow_auth = mode in {"socks5", "reverse_socks"}

        self.set_check_enabled(self.cmd_tunnel_cb, allow_tunnel, self.cmd_use_tunnel)
        self.set_check_enabled(self.cmd_tls_cb, allow_tunnel, self.cmd_use_tls)
        self.set_check_enabled(self.cmd_reconnect_cb, allow_reconnect, self.cmd_use_reconnect)
        self.set_check_enabled(self.cmd_auth_cb, allow_auth, self.cmd_use_auth)
        self.set_check_enabled(self.cmd_quiet_cb, True)

    def set_preview_text(self, text):
        self.cmd_preview.config(state='normal')
        self.cmd_preview.delete("1.0", tk.END)
        self.cmd_preview.insert("1.0", text)
        self.cmd_preview.config(state='disabled')

    def copy_command_preview(self):
        text = self.cmd_preview.get("1.0", tk.END).strip()
        if not text:
            return
        self.root.clipboard_clear()
        self.root.clipboard_append(text)
        self.root.update()
        messagebox.showinfo("RTP", self.get_text("cmd_copy_done"))

    def fmt_tunnel_endpoint(self, host, port, use_tunnel, is_remote=False):
        host = host.strip()
        port = port.strip()

        if is_remote:
            endpoint = f"{host}:{port}" if host else f"<IP>:{port}"
        else:
            endpoint = port if not host else f"{host}:{port}"

        if use_tunnel:
            return "@" + endpoint
        return endpoint

    def append_flag(self, parts, flag, value=None):
        parts.append(flag)
        if value is not None and value != "":
            parts.append(value)

    def maybe_append_crypto_flags(self, parts, use_tunnel):
        key = self.cmd_key.get().strip()
        if not use_tunnel:
            return
        if key:
            self.append_flag(parts, "-k", f"\"{key}\"")
        if self.cmd_use_tls.get():
            self.append_flag(parts, "--tls")

    def maybe_append_common_flags(self, parts, allow_reconnect=False):
        if allow_reconnect and self.cmd_use_reconnect.get():
            self.append_flag(parts, "--reconnect")
        if self.cmd_use_quiet.get():
            self.append_flag(parts, "-q")

    def maybe_append_auth_flags(self, parts):
        if self.cmd_use_auth.get():
            user = self.cmd_user.get().strip()
            password = self.cmd_pass.get().strip()
            if user:
                self.append_flag(parts, "--user", user)
            if password:
                self.append_flag(parts, "--pass", password)

    def build_command_preview(self):
        mode = self.cmd_mode.get()
        server_ip = self.cmd_server_ip.get().strip() or "<SERVER_IP>"
        tunnel_port = self.cmd_tunnel_port.get().strip() or "9998"
        access_port = self.cmd_access_port.get().strip() or "7777"
        local_port = self.cmd_local_port.get().strip() or "1080"
        second_port = self.cmd_second_port.get().strip() or "7777"
        target_host = self.cmd_target_host.get().strip() or "192.168.1.10"
        target_port = self.cmd_target_port.get().strip() or "3389"
        service_port = self.cmd_service_port.get().strip() or "8080"
        use_tunnel = self.cmd_use_tunnel.get()
        lines = []

        if mode == "tcp_forward":
            parts = ["rtp"]
            self.append_flag(parts, "-l", self.fmt_tunnel_endpoint("", local_port, use_tunnel))
            self.append_flag(parts, "-r", self.fmt_tunnel_endpoint(target_host, target_port, use_tunnel, is_remote=True))
            self.maybe_append_crypto_flags(parts, use_tunnel)
            self.maybe_append_common_flags(parts)
            lines.append(" ".join(parts))
        elif mode == "tcp_bridge":
            parts = ["rtp"]
            self.append_flag(parts, "-l", self.fmt_tunnel_endpoint("", local_port, use_tunnel))
            self.append_flag(parts, "-l", self.fmt_tunnel_endpoint("", second_port, use_tunnel))
            self.maybe_append_crypto_flags(parts, use_tunnel)
            self.maybe_append_common_flags(parts)
            lines.append(" ".join(parts))
        elif mode == "socks5":
            parts = ["rtp"]
            self.append_flag(parts, "-l", self.fmt_tunnel_endpoint("", local_port, use_tunnel))
            self.maybe_append_crypto_flags(parts, use_tunnel)
            self.maybe_append_auth_flags(parts)
            self.maybe_append_common_flags(parts)
            lines.append(" ".join(parts))
        elif mode == "reverse_socks":
            server_parts = ["rtp"]
            target_parts = ["rtp"]
            self.append_flag(server_parts, "-l", self.fmt_tunnel_endpoint("", tunnel_port, use_tunnel))
            self.append_flag(server_parts, "-l", access_port)
            self.append_flag(server_parts, "--reverse-socks")
            self.maybe_append_crypto_flags(server_parts, use_tunnel)
            self.maybe_append_common_flags(server_parts)

            self.append_flag(target_parts, "-r", self.fmt_tunnel_endpoint(server_ip, tunnel_port, use_tunnel, is_remote=True))
            self.append_flag(target_parts, "--reverse-socks")
            self.maybe_append_crypto_flags(target_parts, use_tunnel)
            self.maybe_append_auth_flags(target_parts)
            self.maybe_append_common_flags(target_parts, allow_reconnect=True)

            lines.append(self.get_text("cmd_role_server"))
            lines.append(" ".join(server_parts))
            lines.append("")
            lines.append(self.get_text("cmd_role_target"))
            lines.append(" ".join(target_parts))
            lines.append("")
            lines.append(self.get_text("cmd_role_operator"))
            lines.append(self.get_text("cmd_operator_socks_hint").format(server_ip, access_port))
        elif mode == "reverse_service":
            server_parts = ["rtp"]
            target_parts = ["rtp"]
            self.append_flag(server_parts, "-l", self.fmt_tunnel_endpoint("", tunnel_port, use_tunnel))
            self.append_flag(server_parts, "-l", access_port)
            self.maybe_append_crypto_flags(server_parts, use_tunnel)
            self.maybe_append_common_flags(server_parts)

            self.append_flag(target_parts, "-l", service_port)
            self.append_flag(target_parts, "-r", self.fmt_tunnel_endpoint(server_ip, tunnel_port, use_tunnel, is_remote=True))
            self.maybe_append_crypto_flags(target_parts, use_tunnel)
            self.maybe_append_common_flags(target_parts)

            lines.append(self.get_text("cmd_role_server"))
            lines.append(" ".join(server_parts))
            lines.append("")
            lines.append(self.get_text("cmd_role_target"))
            lines.append(" ".join(target_parts))
            lines.append("")
            lines.append(self.get_text("cmd_role_operator"))
            lines.append(self.get_text("cmd_operator_service_hint").format(server_ip, access_port))
        elif mode == "udp":
            parts = ["rtp"]
            self.append_flag(parts, "-l", self.fmt_tunnel_endpoint("", local_port, use_tunnel))
            self.append_flag(parts, "-r", self.fmt_tunnel_endpoint(target_host, target_port, use_tunnel, is_remote=True))
            self.append_flag(parts, "-u")
            self.maybe_append_crypto_flags(parts, use_tunnel)
            self.maybe_append_common_flags(parts)
            lines.append(" ".join(parts))
        else:
            lines.append("rtp -l 1080")

        return "\n".join(lines).strip()

    def render_command_preview(self):
        self.update_command_controls()
        self.set_preview_text(self.build_command_preview())

    def log(self, message):
        self.log_area.config(state='normal')
        self.log_area.insert('end', message + "\n")
        self.log_area.see('end')
        self.log_area.config(state='disabled')

    def start_build_thread(self):
        self.build_btn.config(state='disabled')
        self.log("\n" + "="*50)
        self.log(self.get_text("log_start"))
        threading.Thread(target=self.build_process, daemon=True).start()

    def get_compiler(self, custom_path, os_target, arch):
        if custom_path:
            return custom_path
            
        if os_target == "windows":
            if os.name == 'nt':
                return "gcc"
            else:
                return "x86_64-w64-mingw32-gcc"
        elif os_target == "linux":
            if arch == "x86_64":
                return "gcc" if os.name != 'nt' else "x86_64-linux-gnu-gcc"
            elif arch == "aarch64":
                return "aarch64-linux-gnu-gcc"
            else:
                return "i686-linux-gnu-gcc"
        else: # macos
            return "o64-clang"

    def show_build_success(self, output_paths):
        display_path = output_paths[0] if len(output_paths) == 1 else "\n".join(output_paths)
        self.log(self.get_text("log_success").format(display_path))
        messagebox.showinfo("Success", self.get_text("msg_success").format(display_path))

    def run_compile_command(self, cmd):
        self.log(f"> {' '.join(cmd)}")
        return subprocess.run(cmd, cwd=self.base_dir, capture_output=True, text=True)

    def build_macos_with_zig(self, compiler, src_paths, flags, build_dir, arch):
        slice_specs = []
        if arch == "x86_64":
            slice_specs.append(("x86_64", "x86_64-macos.10.12-none", os.path.join(build_dir, "RTP_macOS_x64")))
        elif arch == "arm64":
            slice_specs.append(("arm64", "aarch64-macos.11.0-none", os.path.join(build_dir, "RTP_macOS_arm64")))
        else:
            slice_specs = [
                ("x86_64", "x86_64-macos.10.12-none", os.path.join(build_dir, "RTP_macOS_x64")),
                ("arm64", "aarch64-macos.11.0-none", os.path.join(build_dir, "RTP_macOS_arm64")),
            ]
        slice_outputs = []

        for slice_name, zig_target, out_path in slice_specs:
            self.log(self.get_text("log_mac_slice").format(slice_name, zig_target))
            cmd = [compiler, "cc", "-target", zig_target] + src_paths + ["-o", out_path] + flags
            result = self.run_compile_command(cmd)
            if result.returncode != 0:
                return False, result.stderr, []
            slice_outputs.append(out_path)

        if arch != "universal":
            return True, "", slice_outputs

        lipo_tool = resolve_llvm_lipo(self.base_dir, compiler)
        if not lipo_tool:
            self.log(self.get_text("log_mac_split_only"))
            return True, "", slice_outputs

        universal_path = os.path.join(build_dir, "RTP_macOS_Universal")
        merge_cmd = [lipo_tool, "-create", "-output", universal_path] + slice_outputs
        self.log(self.get_text("log_mac_merge").format(os.path.basename(lipo_tool)))
        merge_result = self.run_compile_command(merge_cmd)
        if merge_result.returncode != 0:
            return False, merge_result.stderr, []

        return True, "", [universal_path]

    def build_process(self):
        try:
            target = self.target_os.get()
            
            # 1. Mutate
            self.log(self.get_text("log_mutating"))
            header_path = os.path.join(self.base_dir, "include", "mutated.h")
            mappings = generate_obfuscation_header(header_path)
            self.log(mappings)
            
            # 2. Inject
            self.log(self.get_text("log_injecting"))
            inject_include(self.base_dir)
            
            # 3. Compile
            build_dir = os.path.join(self.base_dir, "build")
            os.makedirs(build_dir, exist_ok=True)
            
            arch = self.target_arch.get()
            self.log(self.get_text("log_compiling").format(target, arch))
            
            src_files = ["src/log.c", "src/main.c", "src/net.c", "src/crypto.c", "src/core.c"]
            src_paths = [os.path.join(self.base_dir, f) for f in src_files]
            
            compiler = "gcc"  # Default compiler
            out_file = "RTP"
            flags = ["-I" + os.path.join(self.base_dir, "include"), "-Wall", "-O2", "-g0", "-s"]
            
            if target == "windows":
                out_file = "RTP_Windows.exe"
                flags.append("-lws2_32")
                
                if arch == "legacy":
                    flags.extend(["-DWINVER=0x0502", "-D_WIN32_WINNT=0x0502"]) # Server 2003 / XP compatibility
                    out_file = "RTP_Windows_Legacy.exe"
                else:
                    # For modern Windows, we don't strictly need to force WINVER on the command line if the compiler provides it,
                    # but to be safe and avoid redefining, we only pass it if not already defined or we suppress the warning
                    flags.extend(["-DWINVER=0x0A00", "-D_WIN32_WINNT=0x0A00", "-Wno-macro-redefined"]) # Win10+
                    out_file = "RTP_Windows_Modern.exe"
                    
                if os.name != 'nt':
                    compiler = "x86_64-w64-mingw32-gcc"
            elif target == "linux":
                flags.append("-static-libgcc")
                if arch == "x86_64":
                    compiler = "gcc" if os.name != 'nt' else "x86_64-linux-gnu-gcc"
                    out_file = "RTP_Linux_x64"
                elif arch == "aarch64":
                    compiler = "aarch64-linux-gnu-gcc"
                    out_file = "RTP_Linux_arm64"
                elif arch == "i686":
                    compiler = "i686-linux-gnu-gcc"
                    flags.append("-m32")
                    out_file = "RTP_Linux_x86"
                    
                if os.name == 'nt' and compiler == "gcc":
                    self.log(self.get_text("log_warn_linux"))
            elif target == "macos":
                if arch == "arm64":
                    out_file = "RTP_macOS_arm64"
                elif arch == "universal":
                    out_file = "RTP_macOS_Universal"
                else:
                    out_file = "RTP_macOS_x64"
                if sys.platform != 'darwin':
                    self.log(self.get_text("log_warn_mac"))
                    compiler = "o64-clang" # osxcross
                    
            # Override with custom compiler if provided
            custom_cc = self.custom_cc_var.get().strip()
            if custom_cc:
                if not os.path.exists(custom_cc) and not os.path.isfile(custom_cc):
                    # Check if it might be a command in PATH instead of absolute path
                    # but typically if they type it here, they mean a path.
                    # We'll do a soft check.
                    if os.path.sep in custom_cc:
                        self.log(self.get_text("log_err_cc_path").format(custom_cc))
                        self.log(self.get_text("log_fail"))
                        messagebox.showerror("Error", self.get_text("log_err_cc_path").format(custom_cc))
                        return
                compiler = custom_cc
                self.log(f"[*] Using custom compiler: {compiler}")

            out_path = os.path.join(build_dir, out_file)
            
            # Zig cc drop-in replacement support
            is_zig = "zig" in compiler.lower()
            if is_zig and target == "macos":
                success, stderr_text, output_paths = self.build_macos_with_zig(compiler, src_paths, flags, build_dir, arch)
                if success:
                    self.show_build_success(output_paths)
                else:
                    self.log(self.get_text("log_fail"))
                    self.log(stderr_text)
                    messagebox.showerror("Build Error", self.get_text("msg_fail"))
                return

            cmd_base = [compiler, "cc"] if is_zig else [compiler]
            
            if is_zig:
                flags = [flag for flag in flags if flag != "-static-libgcc"]
                if target == "windows":
                    cmd_base.extend(["-target", "x86_64-windows-gnu"])
                elif target == "linux":
                    if arch == "x86_64":
                        cmd_base.extend(["-target", "x86_64-linux-musl"])
                    elif arch == "aarch64":
                        cmd_base.extend(["-target", "aarch64-linux-musl"])
                    else:
                        cmd_base.extend(["-target", "x86-linux-musl"])
                else:
                    if arch == "arm64":
                        cmd_base.extend(["-target", "aarch64-macos.11.0-none"])
                    else:
                        cmd_base.extend(["-target", "x86_64-macos.10.12-none"])

            cmd = cmd_base + src_paths + ["-o", out_path] + flags
            
            try:
                result = self.run_compile_command(cmd)
                
                if result.returncode == 0:
                    self.show_build_success([out_path])
                else:
                    self.log(self.get_text("log_fail"))
                    self.log(result.stderr)
                    messagebox.showerror("Build Error", self.get_text("msg_fail"))
            except FileNotFoundError:
                self.log(self.get_text("log_fail"))
                err_msg = self.get_text("log_warn_cc").format(compiler)
                self.log(err_msg)
                messagebox.showerror("Compiler Not Found", err_msg)
                
        except Exception as e:
            self.log(f"[-] ERROR: {str(e)}")
        finally:
            self.root.after(0, lambda: self.build_btn.config(state='normal'))

if __name__ == "__main__":
    root = tk.Tk()
    app = RTPBuilderGUI(root)
    root.mainloop()
