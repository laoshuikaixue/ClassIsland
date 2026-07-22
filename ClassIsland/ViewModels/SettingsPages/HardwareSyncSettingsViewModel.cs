using System;
using System.Threading.Tasks;
using ClassIsland.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace ClassIsland.ViewModels.SettingsPages;

public partial class HardwareSyncSettingsViewModel(
    SettingsService settingsService,
    HardwareSyncService hardwareSyncService) : ObservableObject
{
    public SettingsService SettingsService { get; } = settingsService;

    [ObservableProperty] private bool _isTestingDot;
    [ObservableProperty] private string _dotTestStatus = "尚未测试 Dot 连接。";

    [RelayCommand]
    private async Task TestDotAsync()
    {
        if (IsTestingDot)
            return;

        IsTestingDot = true;
        DotTestStatus = "正在生成并推送当前课表…";
        try
        {
            var result = await hardwareSyncService.TestDotSyncAsync();
            DotTestStatus = result.Message;
        }
        catch (Exception)
        {
            DotTestStatus = "测试 Dot 连接时发生未知错误，请检查配置和网络后重试。";
        }
        finally
        {
            IsTestingDot = false;
        }
    }
}
