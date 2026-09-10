#include "pch.h"
#include "LogsPage.xaml.h"
#if __has_include("LogsPage.g.cpp")
#include "LogsPage.g.cpp"
#endif

#include "../Services/AppLog.h"
#include "../Services/CoreSupervisor.h"
#include "../Services/Localization.h"
#include "StateUi.h"

#include <winrt/Windows.ApplicationModel.DataTransfer.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Services;

namespace winrt::OpenRung::WinUI::implementation
{
    LogsPage::LogsPage()
    {
        InitializeComponent();

        auto& store = Services::LogStore::Instance();
        LogList().ItemsSource(store.Lines());
        m_vectorToken = store.Lines().VectorChanged(
            [this](Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> const&,
                Windows::Foundation::Collections::IVectorChangedEventArgs const&) {
                ScrollToBottom();
            });
        AutoScrollToggle().IsChecked(store.AutoScroll());

        ApplyStrings();

        // Seed: the stream replays backlog before live lines; this page may be
        // opened later, so pull a tail snapshot on first load.
        Loaded([this](Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
            SeedLogs();
        });
    }

    void LogsPage::ApplyStrings()
    {
        ClearText().Text(I18n::Tr(L"logs.clear"));
        CopyText().Text(I18n::Tr(L"logs.copy"));
        AutoScrollToggle().Content(box_value(winrt::hstring(I18n::Tr(L"logs.autoScroll"))));
    }

    LogsPage::~LogsPage()
    {
        m_lifetime.End();
        if (m_vectorToken.value != 0)
            Services::LogStore::Instance().Lines().VectorChanged(m_vectorToken);
    }

    void LogsPage::ScrollToBottom()
    {
        if (!Services::LogStore::Instance().AutoScroll())
            return;
        auto items = LogList().Items();
        if (items.Size() > 0)
            LogList().ScrollIntoView(items.GetAt(items.Size() - 1));
    }

    void LogsPage::SeedLogs()
    {
        if (Services::LogStore::Instance().Lines().Size() > 0)
            return;
        std::thread([weak = m_lifetime.Weak()] {
            try
            {
                auto logs = Services::CoreSupervisor::Instance().Core().EnsureRunning(false).GetLogs(500);
                for (auto const& log : logs)
                    Services::AppLog::AppendCoreLine(log.time, log.line);
            }
            catch (...)
            {
                // engine unreachable; stream will feed live lines anyway
            }
        }).detach();
    }

    void LogsPage::Clear_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        Services::LogStore::Instance().Clear();
    }

    void LogsPage::Copy_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        auto lines = Services::LogStore::Instance().Lines();
        std::wstring joined;
        for (uint32_t i = 0; i < lines.Size(); ++i)
        {
            if (i > 0)
                joined += L"\r\n";
            joined += std::wstring(winrt::unbox_value<winrt::hstring>(lines.GetAt(i)));
        }

        Windows::ApplicationModel::DataTransfer::DataPackage package;
        package.SetText(winrt::hstring(joined));
        Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
    }

    void LogsPage::AutoScrollToggle_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        auto on = AutoScrollToggle().IsChecked().GetBoolean();
        Services::LogStore::Instance().AutoScroll(on);
        if (on)
            ScrollToBottom();
    }
}
