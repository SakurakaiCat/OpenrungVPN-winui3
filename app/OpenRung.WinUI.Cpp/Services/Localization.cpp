#include "pch.h"
#include "Localization.h"
#include "AppSettings.h"

namespace Services
{
    namespace
    {
        struct Entry
        {
            wchar_t const* key;
            wchar_t const* zh;
            wchar_t const* en;
        };

        // The string table. Chinese first (source language of the upstream
        // client); English translations alongside.
        Entry const k_entries[] = {
            // shell
            {L"nav.home", L"主页", L"Home"},
            {L"nav.servers", L"服务器", L"Servers"},
            {L"nav.logs", L"日志", L"Logs"},
            {L"nav.settings", L"设置", L"Settings"},
            {L"tray.open", L"打开窗口", L"Open window"},
            {L"tray.quit", L"退出", L"Quit"},

            // home page
            {L"home.nodeHeader", L"节点", L"Nodes"},
            {L"home.autoSelectTip", L"自动选择延迟最低的节点", L"Automatically pick the lowest-latency node"},
            {L"home.refreshTip", L"从远端更新节点列表并测速", L"Update the node list from the remote and measure latency"},
            {L"home.booting", L"正在启动核心…", L"Starting core…"},
            {L"home.notConnected", L"未连接", L"Not connected"},
            {L"home.errorTitle", L"错误", L"Error"},
            {L"relay.unnamed", L"节点", L"Node"},
            {L"relay.summary", L"{0} 个节点，已测速 {1} 个", L"{0} nodes, {1} ranked"},
            {L"row.notTested", L"未测速", L"Not tested"},
            {L"row.tcpingFailed", L"TCPing 失败", L"TCPing failed"},
            {L"row.realDelay", L"真延迟", L"Real-delay"},
            {L"row.realDelayFailed", L"真延迟 失败", L"Real-delay failed"},
            {L"dlg.intercepted405", L"本机有代理/VPN 拦截了发往核心的请求（{0} → 405）。请退出其 TUN/透明代理模式或整个客户端后重试（详见上方 [诊断] 行）。",
             L"A local proxy/VPN intercepted requests to the core ({0} → 405). Disable its TUN/transparent mode or quit it, then retry (see the [diag] lines above)."},
            {L"mode.proxy", L"代理模式", L"Proxy"},
            {L"mode.tun", L"TUN 模式", L"TUN"},

            // servers page
            {L"servers.updateRemote", L"从远端更新节点", L"Update nodes from remote"},
            {L"servers.autoSelect", L"自动选择最低延迟", L"Auto-select lowest latency"},
            {L"servers.tcping", L"TCPing 测试", L"TCPing test"},
            {L"servers.realDelay", L"真延迟测试", L"Real-delay test"},
            {L"servers.testHint", L"TCPing：并发握手测速；真延迟：逐个节点经隧道实测（需断开连接）",
             L"TCPing: concurrent handshake latency; real delay: measured per node through the tunnel (disconnect first)"},
            {L"servers.errorTitle", L"刷新失败", L"Refresh failed"},
            {L"servers.tcpingRunning", L"TCPing 测试中…", L"Running TCPing test…"},
            {L"servers.tcpingDone", L"TCPing 完成：{0}/{1} 成功", L"TCPing done: {0}/{1} succeeded"},
            {L"servers.tcpingFailed", L"TCPing 失败：{0}", L"TCPing failed: {0}"},
            {L"servers.realDelayProgress", L"真延迟 {0}/{1}：{2}", L"Real delay {0}/{1}: {2}"},
            {L"servers.realDelayDone", L"真延迟完成：{0}/{1} 成功", L"Real delay done: {0}/{1} succeeded"},
            {L"servers.realDelayNeedDisconnect", L"真延迟测试需要先断开连接（当前状态：{0}）",
             L"Real-delay testing requires being disconnected (current state: {0})"},
            {L"servers.realDelayTunBlocked", L"TUN 模式下无法逐节点测试，请先切回代理模式",
             L"Per-node testing is unavailable in TUN mode; switch back to proxy mode first"},
            {L"servers.firstFailure", L"（首个失败：{0}）", L" (first failure: {0})"},

            // settings page
            {L"settings.title", L"设置", L"Settings"},
            {L"settings.captureMode", L"捕获模式", L"Capture mode"},
            {L"settings.captureModeDesc", L"代理模式仅接管系统代理；TUN 模式接管全设备流量，需要管理员权限。",
             L"Proxy mode only takes over the system proxy; TUN mode captures all device traffic and requires administrator rights."},
            {L"settings.modeProxy", L"代理模式", L"Proxy mode"},
            {L"settings.modeTun", L"TUN 模式", L"TUN mode"},
            {L"settings.systemProxy", L"系统代理", L"System proxy"},
            {L"settings.autoClear", L"启动时自动清除已有系统代理", L"Clear an existing system proxy on startup"},
            {L"settings.clearNow", L"立即清除系统代理", L"Clear system proxy now"},
            {L"settings.core", L"核心", L"Core"},
            {L"settings.coreManager", L"核心管理", L"Core control"},
            {L"settings.coreStart", L"启动核心", L"Start core"},
            {L"settings.coreStop", L"停止核心", L"Stop core"},
            {L"settings.coreRestart", L"重启核心", L"Restart core"},
            {L"settings.coreRunning", L"核心运行中（PID {0}）", L"Core running (PID {0})"},
            {L"settings.coreStopped", L"核心已停止", L"Core stopped"},
            {L"settings.coreStarting", L"正在启动核心…", L"Starting core…"},
            {L"settings.coreStopping", L"正在停止核心…", L"Stopping core…"},
            {L"settings.coreRestarting", L"正在重启核心…", L"Restarting core…"},
            {L"settings.coreStartFailed", L"启动失败：{0}", L"Start failed: {0}"},
            {L"settings.language", L"语言", L"Language"},
            {L"settings.relays", L"节点列表", L"Node list"},
            {L"settings.hideCn", L"隐藏中国大陆节点", L"Hide mainland China nodes"},
            {L"settings.hideCnDesc", L"中国大陆节点由志愿者提供，可用性极差，默认不在节点列表中显示。如需使用，请自行评估其安全性与合规风险。",
             L"Mainland China nodes are volunteer-provided with very poor availability and are hidden from the node lists by default. If you enable them, evaluate their security and compliance yourself."},
            {L"settings.dns", L"DNS", L"DNS"},
            {L"settings.dnsDesc", L"隧道内域名解析使用的 DNS 服务器；修改在下次连接时生效。",
             L"DNS servers used for name resolution inside the tunnel; changes apply on the next connect."},
            {L"settings.dnsAuto", L"自动（1.1.1.1 / 8.8.8.8）", L"Automatic (1.1.1.1 / 8.8.8.8)"},
            {L"settings.dnsCustom", L"自定义", L"Custom"},
            {L"settings.dnsCustomPlaceholder", L"逗号分隔，最多 4 个 IP，例如 1.1.1.1, 8.8.8.8",
             L"Comma-separated, up to 4 IPs, e.g. 1.1.1.1, 8.8.8.8"},
            {L"settings.dnsSave", L"保存", L"Save"},
            {L"settings.ipv6", L"IPv6", L"IPv6"},
            {L"settings.on", L"开", L"On"},
            {L"settings.off", L"关", L"Off"},
            {L"settings.dnsIpv6Desc", L"关闭后隧道仅接管 IPv4 流量，DNS 不再解析 AAAA 记录。",
             L"When off, the tunnel carries IPv4 traffic only and AAAA lookups are disabled."},
            {L"settings.dnsSaved", L"DNS 设置已保存，下次连接时生效。", L"DNS settings saved; they apply on the next connect."},
            {L"settings.dnsLoadFailed", L"读取 DNS 设置失败：{0}", L"Failed to load DNS settings: {0}"},
            {L"settings.langSystem", L"跟随系统", L"Follow system"},
            {L"settings.langZh", L"简体中文", L"简体中文"},
            {L"settings.langEn", L"English", L"English"},
            {L"settings.about", L"关于", L"About"},
            {L"settings.aboutMaintainer", L"OpenRung VPN WinUI3 客户端 · 维护者 @SakurakaiCat",
             L"OpenRung VPN WinUI3 client · maintained by @SakurakaiCat"},
            {L"settings.aboutDev", L"OpenRung VPN WinUI3 由 @SakurakaiCat 基于 OpenRung 仓库二次开发。",
             L"OpenRung VPN WinUI3 is developed by @SakurakaiCat based on the upstream OpenRung repository."},
            {L"settings.copyright", L"Copyright © 2026 @SakurakaiCat。本客户端基于上游 OpenRung 的修改作品，与上游 OpenRung 均以 GNU General Public License v3.0（GPL-3.0）发布，详见仓库根目录的 LICENSE 文件。",
             L"Copyright © 2026 @SakurakaiCat. This client is a modified work of the upstream OpenRung; both are released under the GNU General Public License v3.0 (GPL-3.0) — see the LICENSE file at the repository root."},
            {L"settings.checkUpdate", L"检查更新", L"Check for updates"},
            {L"settings.checkingUpdate", L"正在检查更新…", L"Checking for updates…"},
            {L"settings.upToDate", L"已是最新版本", L"Up to date"},
            {L"settings.upToDateStatus", L"已是最新版本", L"Up to date"},
            {L"settings.curVersion", L"当前版本：", L"Current version: "},
            {L"settings.endpoint", L"127.0.0.1:{0}（PID {1}）", L"127.0.0.1:{0} (PID {1})"},
            {L"settings.aboutLine1", L"OpenRung VPN WinUI3 客户端 · 维护者 @SakurakaiCat",
             L"OpenRung VPN WinUI3 client · maintained by @SakurakaiCat"},
            {L"settings.aboutLine2", L" 由 @SakurakaiCat 基于 OpenRung 仓库二次开发。",
             L" by @SakurakaiCat, based on the upstream OpenRung repository."},
            {L"settings.aboutPrefix", L"OpenRung VPN WinUI3 由 ", L"OpenRung VPN WinUI3 is developed by "},
            {L"settings.aboutSuffix", L" 基于 OpenRung 仓库二次开发。", L", based on the upstream OpenRung repository."},
            {L"settings.license", L"Copyright © 2026 @SakurakaiCat。本客户端基于上游 OpenRung 的修改作品，与上游 OpenRung 均以 GNU General Public License v3.0（GPL-3.0）发布，详见仓库根目录的 LICENSE 文件。",
             L"Copyright © 2026 @SakurakaiCat. This client is a modified work of the upstream OpenRung; both are licensed under the GNU General Public License v3.0 (GPL-3.0) — see the LICENSE file at the repository root."},
            {L"settings.repoUpstream", L"OpenRung 上游仓库 · github.com/openrung/openrung",
             L"Upstream OpenRung repo · github.com/openrung/openrung"},
            {L"settings.repoClient", L"本客户端仓库 · github.com/SakurakaiCat/OpenrungVPN-winui3",
             L"This client's repo · github.com/SakurakaiCat/OpenrungVPN-winui3"},
            {L"settings.systemProxyText", L"当前系统代理：{0}。代理模式下内核会先接管已有代理、断开时恢复；开启自动清除后，启动时直接移除已有代理（例如其他代理工具留下的）。",
             L"Current system proxy: {0}. In proxy mode the engine takes over an existing proxy first and restores it on disconnect; with auto-clear enabled, an existing proxy (e.g. left by another proxy tool) is removed at startup."},

            // logs page
            {L"logs.clear", L"清空", L"Clear"},
            {L"logs.copy", L"复制", L"Copy"},
            {L"logs.autoScroll", L"自动滚动", L"Auto-scroll"},

            // dialogs
            {L"dlg.close", L"关闭", L"Close"},
            {L"dlg.cancel", L"取消", L"Cancel"},
            {L"dlg.ok", L"好", L"OK"},
            {L"dlg.elevTitle", L"需要管理员权限", L"Administrator rights required"},
            {L"dlg.elevBody", L"TUN 模式需要以管理员身份运行核心进程。\n\n{0}",
             L"TUN mode requires running the core process as administrator.\n\n{0}"},
            {L"dlg.elevRestartCore", L"以管理员身份重启核心", L"Restart core as administrator"},
            {L"dlg.elevOffer", L"\n\n是否重启核心为管理员模式并启用 TUN？",
             L"\n\nRestart the core as administrator and enable TUN?"},
            {L"dlg.grantAndRestart", L"授予并重启核心", L"Grant and restart core"},
            {L"dlg.restartFailedTitle", L"重启失败", L"Restart failed"},
            {L"dlg.modeFailedTitle", L"无法切换模式", L"Can't switch mode"},
            {L"dlg.updateFoundTitle", L"发现新版本 {0}", L"New version {0} found"},
            {L"dlg.updateBody", L"当前版本 {0}，最新版本 {1}。\n\n请前往 GitHub Releases 页面下载新的压缩包并解压替换。",
             L"Current version {0}, latest version {1}.\n\nPlease download the new package from the GitHub Releases page and replace the current install."},
            {L"dlg.goDownload", L"前往下载", L"Download"},
            {L"dlg.updateCheckFailedTitle", L"检查更新失败", L"Update check failed"},
            {L"dlg.nowVersion", L"当前版本 ", L"Current version "},
            {L"dlg.latestVersion", L"，最新版本 ", L", latest version "},
            {L"dlg.langChanged", L"语言已切换，界面已即时应用。", L"Language switched and applied immediately."},

            {L"dlg.connectFailed", L"无法连接核心进程，请稍后重试。", L"Cannot reach the core process; try again shortly."},
            {L"servers.realDelayRunning", L"真延迟测试中…", L"Running real-delay test…"},

            // app log lines (user-visible on the logs page)
            {L"log.startingCore", L"启动核心...", L"Starting core..."},
            {L"log.coreStarted", L"核心已启动", L"Core started"},
            {L"log.coreStartFailed", L"核心启动失败:", L"Core start failed: "},
            {L"log.coreStopped", L"核心已停止", L"Core stopped"},
            {L"log.coreStopFailed", L"核心停止失败:", L"Core stop failed: "},
            {L"log.coreRestarted", L"核心已重启", L"Core restarted"},
            {L"log.coreRestartFailed", L"核心重启失败:", L"Core restart failed: "},
            {L"log.relayUpdateDone", L"从远端更新节点完成:{0} 个节点", L"Remote node update finished: {0} nodes"},
            {L"log.relayUpdateFailed", L"从远端更新节点失败:", L"Remote node update failed: "},
            {L"log.cnRelaysHidden", L"已隐藏 {0} 个中国大陆节点（设置中可关闭）", L"Hidden {0} mainland China node(s); toggle in Settings"},

            // settings status lines
            {L"status.checking", L"正在检查更新…", L"Checking for updates…"},
            {L"status.checkFailed", L"检查失败", L"Check failed"},
            {L"settings.curVer", L"当前版本：{0}", L"Current version: {0}"},
            {L"settings.checkUpdates", L"检查更新", L"Check for updates"},
            {L"settings.coreManagerHint", L"重启核心会保留当前提权状态（TUN 模式下将重新弹出 UAC 确认）。",
             L"Restarting the core keeps its current elevation (TUN will prompt UAC again)."},
            {L"settings.endpointFmt", L"{0}（PID {1}）", L"{0} (PID {1})"},

            // update + mode dialogs
            {L"dlg.upToDateStatus", L"已是最新版本", L"Up to date"},
            {L"dlg.updateTitle", L"检查更新", L"Check for updates"},
            {L"dlg.upToDateBody", L"当前已是最新版本（{0}）。", L"Already the latest version ({0})."},
            {L"dlg.updateFound", L"发现新版本 {0}", L"New version {0} found"},
            {L"dlg.updateBody", L"当前版本 {0}，最新版本 {1}。\n\n请前往 GitHub Releases 页面下载新的压缩包并解压替换。",
             L"Current version {0}, latest version {1}.\n\nDownload the new archive from the GitHub Releases page and replace the old files."},
            {L"dlg.updateGo", L"前往下载", L"Go to download"},
            {L"dlg.updateCheckFailTitle", L"检查更新失败", L"Update check failed"},
            {L"dlg.modeFailTitle", L"无法切换模式", L"Cannot switch mode"},
            {L"dlg.dnsFailTitle", L"无法修改 DNS", L"Can't change DNS"},
            {L"dlg.restartCore", L"重启核心", L"Restart core"},
            {L"dlg.restartFailTitle", L"重启失败", L"Restart failed"},
            {L"dlg.elevTunBody", L"{0}\n\n是否重启内核为管理员模式并启用 TUN？",
             L"{0}\n\nRestart the engine as administrator and enable TUN?"},
        };

        std::wstring g_language = L"zh";   // active
        std::wstring g_preference = L"system";

        Entry const* Find(std::wstring_view key)
        {
            for (auto const& e : k_entries)
                if (key == e.key)
                    return &e;
            return nullptr;
        }

        std::wstring DetectSystemLanguage()
        {
            // Primary UI language; zh -> Chinese, anything else -> English.
            LANGID lang = ::GetUserDefaultUILanguage();
            WORD primary = PRIMARYLANGID(lang);
            return primary == LANG_CHINESE ? L"zh" : L"en";
        }

        void ResolveActive()
        {
            g_language = (g_preference == L"zh" || g_preference == L"en")
                ? g_preference
                : DetectSystemLanguage();
        }
    }

    namespace I18n
    {
        void Init()
        {
            g_preference = AppSettings::Load().language;
            if (g_preference.empty())
                g_preference = L"system";
            ResolveActive();
        }

        std::wstring const& Language() { return g_language; }
        std::wstring const& Preference() { return g_preference; }

        void SetPreference(std::wstring const& choice)
        {
            g_preference = choice.empty() ? L"system" : choice;

            auto settings = AppSettings::Load();
            settings.language = g_preference;
            settings.Save();

            ResolveActive();
            if (LanguageChanged)
                LanguageChanged();
        }

        std::wstring Tr(std::wstring_view key)
        {
            if (auto const* e = Find(key))
                return g_language == L"en" ? e->en : e->zh;
            return std::wstring(key);
        }

        std::wstring Tr(std::wstring_view key, std::wstring const& a0,
            std::wstring const& a1, std::wstring const& a2)
        {
            auto text = Tr(key);
            std::wstring const placeholders[] = {L"{0}", L"{1}", L"{2}"};
            std::wstring const* args[] = {&a0, &a1, &a2};
            for (size_t i = 0; i < 3; ++i)
            {
                if (args[i]->empty())
                    continue;
                // Single pass: each match appends to `out` and the search
                // resumes after the placeholder, so inserted text is never
                // rescanned. A while(find/replace) loop here hangs the UI
                // thread forever when an argument itself contains "{i}"
                // (e.g. a template passed through as an argument).
                std::wstring out;
                out.reserve(text.size() + args[i]->size());
                size_t pos = 0;
                for (;;)
                {
                    size_t at = text.find(placeholders[i], pos);
                    if (at == std::wstring::npos)
                    {
                        out.append(text, pos, std::wstring::npos);
                        break;
                    }
                    out.append(text, pos, at - pos);
                    out.append(*args[i]);
                    pos = at + placeholders[i].size();
                }
                text = std::move(out);
            }
            return text;
        }
    }
}
