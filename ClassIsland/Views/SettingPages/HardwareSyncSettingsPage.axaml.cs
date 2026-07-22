using ClassIsland.Core.Abstractions.Controls;
using ClassIsland.Core.Attributes;
using ClassIsland.Core.Enums.SettingsWindow;
using ClassIsland.Shared;
using ClassIsland.ViewModels.SettingsPages;

namespace ClassIsland.Views.SettingPages;

[Group("classisland.general")]
[SettingsPageInfo("hardware-sync", "硬件同步", "\ue4a9", "\ue4a8", SettingsPageCategory.Internal)]
public partial class HardwareSyncSettingsPage : SettingsPageBase
{
    public HardwareSyncSettingsViewModel ViewModel { get; } = IAppHost.GetService<HardwareSyncSettingsViewModel>();

    public HardwareSyncSettingsPage()
    {
        DataContext = this;
        InitializeComponent();
    }
}
