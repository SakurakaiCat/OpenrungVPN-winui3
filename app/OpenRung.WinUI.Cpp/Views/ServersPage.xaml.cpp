#include "pch.h"
#include "ServersPage.xaml.h"
#if __has_include("ServersPage.g.cpp")
#include "ServersPage.g.cpp"
#endif

#include "../Models/Dto.h"
#include "../Services/RelayDirectory.h"
#include "StateUi.h"
#include "../Models/RelayRow.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::OpenRung::WinUI::implementation
{
    ServersPage::ServersPage()
    {
        InitializeComponent();

        // Lifetime-guarded: in-flight store notifications must not touch a
        // page that navigation has already destroyed.
        Services::RelayStore::Instance().AddListener(&m_storeKey, [this, weak = m_lifetime.Weak()] {
            if (!StateUi::Lifetime::Live(weak)) return;
            OnStoreChanged();
        });
        OnStoreChanged();
    }

    ServersPage::~ServersPage()
    {
        m_lifetime.End();
        Services::RelayStore::Instance().RemoveListener(&m_storeKey);
    }

    void ServersPage::OnNavigatedTo(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e)
    {
        __super::OnNavigatedTo(e);
        if (Services::RelayStore::Instance().Relays().empty())
            Reload();
    }

    void ServersPage::Refresh_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        Reload();
    }

    void ServersPage::AutoSelect_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        Services::RelayDirectory::SelectLowestLatency();
    }

    void ServersPage::RelayList_SelectionChanged(Windows::Foundation::IInspectable const&,
        SelectionChangedEventArgs const&)
    {
        if (m_suppressSelection)
            return;
        if (auto row = RelayList().SelectedItem().try_as<winrt::OpenRung::WinUI::RelayRow>())
            Services::RelayStore::Instance().SetSelectedId(std::wstring(row.Id()));
    }

    void ServersPage::Reload()
    {
        auto& store = Services::RelayStore::Instance();
        store.SetLoading(true);
        store.SetError(L"");
        std::thread([&store, weak = m_lifetime.Weak()] {
            try
            {
                Services::RelayDirectory::Load();
            }
            catch (std::exception const& ex)
            {
                store.SetError(Services::Utf8ToWide(ex.what()));
            }
            store.SetLoading(false);
        }).detach();
    }

    void ServersPage::OnStoreChanged()
    {
        auto& store = Services::RelayStore::Instance();

        auto loading = store.Loading();
        LoadingBar().IsIndeterminate(loading);
        LoadingBar().Visibility(loading ? Visibility::Visible : Visibility::Collapsed);

        auto error = store.Error();
        ErrorBar().Message(winrt::hstring(error));
        ErrorBar().IsOpen(!error.empty());

        SummaryText().Text(store.SummaryText());

        auto relays = store.Relays();
        auto selected = store.SelectedId();
        // Same bindable-vector requirement as HomePage.OnStoreChanged.
        auto rows = winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>();
        int selectedIndex = -1;
        for (size_t i = 0; i < relays.size(); ++i)
        {
            auto row = StateUi::MakeRelayRow(relays[i]);
            if (!selected.empty() && std::wstring(row.Id()) == selected)
                selectedIndex = static_cast<int>(i);
            rows.Append(std::move(row));
        }

        m_suppressSelection = true;
        RelayList().ItemsSource(rows);
        if (selectedIndex >= 0)
            RelayList().SelectedIndex(selectedIndex);
        m_suppressSelection = false;
    }
}
