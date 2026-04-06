using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Json;
using System.Threading.Tasks;
using System.Text.Json;
using ClassIsland.Core.Abstractions.Services;
using ClassIsland.Shared.Models.Profile;
using Microsoft.Extensions.Logging;
using Plugin.BLE;
using Plugin.BLE.Abstractions.Contracts;
using Plugin.BLE.Abstractions.Exceptions;

namespace ClassIsland.Services;

/// <summary>
/// 硬件同步服务，用于将课程信息上传到 Flask 服务器。
/// </summary>
public class HardwareSyncService
{
    private ILessonsService LessonsService { get; }
    private SettingsService SettingsService { get; }
    private IProfileService ProfileService { get; }
    private IWeatherService WeatherService { get; }
    private ILogger<HardwareSyncService> Logger { get; }
    private HttpClient HttpClient { get; } = new();

    private DateTime _lastUploadDate = DateTime.MinValue;
    private DateTime _lastWeatherUpdateTime = DateTime.MinValue;
    private DateTime _lastBluetoothSyncTime = DateTime.MinValue;

    public HardwareSyncService(
        ILessonsService lessonsService,
        SettingsService settingsService,
        IProfileService profileService,
        IWeatherService weatherService,
        ILogger<HardwareSyncService> logger)
    {
        LessonsService = lessonsService;
        SettingsService = settingsService;
        ProfileService = profileService;
        WeatherService = weatherService;
        Logger = logger;

        // 订阅主计时器，在每次更新后检查是否需要同步
        LessonsService.PostMainTimerTicked += LessonsServiceOnPostMainTimerTicked;
    }

    private void LessonsServiceOnPostMainTimerTicked(object? sender, EventArgs e)
    {
        if (!SettingsService.Settings.IsHardwareSyncEnabled)
            return;

        bool shouldSync = false;
        
        // 每天上传一次，或者在课表变更时上传
        if (DateTime.Today != _lastUploadDate)
        {
            shouldSync = true;
        }
        else if (SettingsService.Settings.LastWeatherInfo != null && 
                 SettingsService.Settings.LastWeatherInfo.UpdateTime != _lastWeatherUpdateTime)
        {
            shouldSync = true;
        }

        // 蓝牙同步每 5 秒尝试一次，确保 ESP32 随时能收到数据
        bool shouldSyncBluetooth = false;
        if ((DateTime.Now - _lastBluetoothSyncTime).TotalSeconds >= 5)
        {
            shouldSyncBluetooth = true;
        }

        if (shouldSync || shouldSyncBluetooth)
        {
            _ = SyncDataAsync(shouldSync, shouldSyncBluetooth);
            if (shouldSync)
            {
                _lastWeatherUpdateTime = SettingsService.Settings.LastWeatherInfo?.UpdateTime ?? DateTime.MinValue;
            }
            if (shouldSyncBluetooth)
            {
                _lastBluetoothSyncTime = DateTime.Now;
            }
        }
    }

    public async Task SyncDataAsync(bool syncNetwork = true, bool syncBluetooth = true)
    {
        try
        {
            var plan = LessonsService.CurrentClassPlan;
            if (plan == null)
            {
                Logger.LogInformation("当前没有加载的课表，跳过硬件同步。");
                return;
            }

            var courses = new List<object>();
            foreach (var classInfo in plan.Classes)
            {
                if (!ProfileService.Profile.Subjects.TryGetValue(classInfo.SubjectId, out var subject))
                    continue;

                var timeItem = classInfo.CurrentTimeLayoutItem;
                courses.Add(new
                {
                    name = subject.Name,
                    startTime = timeItem.StartTime.ToString(@"hh\:mm"),
                    endTime = timeItem.EndTime.ToString(@"hh\:mm")
                });
            }

            var weatherInfo = SettingsService.Settings.LastWeatherInfo;
            var rainMin = weatherInfo?.Minutely?.Precipitation?.RainRemainingMinutes ?? 0;
            string rainMsg = "";
            if (rainMin > 0)
            {
                rainMsg = $"预计 {ClassIsland.Converters.MinutesToApproxTimeConverter.Instance.Convert(rainMin, typeof(string), null, System.Globalization.CultureInfo.CurrentCulture)} 后下雨";
            }
            else if (rainMin < 0)
            {
                rainMsg = $"预计 {ClassIsland.Converters.MinutesToApproxTimeConverter.Instance.Convert(-rainMin, typeof(string), null, System.Globalization.CultureInfo.CurrentCulture)} 后雨停";
            }

            var tomorrowCourses = new List<object>();
            var tomorrowPlan = LessonsService.GetClassPlanByDate(DateTime.Today.AddDays(1), out _);
            if (tomorrowPlan != null)
            {
                foreach (var classInfo in tomorrowPlan.Classes)
                {
                    if (!ProfileService.Profile.Subjects.TryGetValue(classInfo.SubjectId, out var subject))
                        continue;

                    var timeItem = classInfo.CurrentTimeLayoutItem;
                    tomorrowCourses.Add(new
                    {
                        name = subject.Name,
                        startTime = timeItem.StartTime.ToString(@"hh\:mm"),
                        endTime = timeItem.EndTime.ToString(@"hh\:mm")
                    });
                }
            }

            var data = new
            {
                date = DateTime.Today.ToString("yyyy-MM-dd"),
                timestamp = DateTimeOffset.Now.ToUnixTimeSeconds(),
                weather = new
                {
                    text = weatherInfo != null ? WeatherService.GetWeatherTextByCode(weatherInfo.Current.Weather) : "未知",
                    temp = weatherInfo?.Current?.Temperature?.Value ?? "0",
                    rain = rainMsg,
                    warning = weatherInfo?.Alerts?.FirstOrDefault()?.Title ?? ""
                },
                courses = courses,
                tomorrowCourses = tomorrowCourses
            };

            // 发送数据到蓝牙设备（非阻塞）
            if (syncBluetooth)
            {
                _ = Task.Run(() => SendViaBluetoothAsync(data));
            }

            if (syncNetwork)
            {
                using var request = new HttpRequestMessage(HttpMethod.Post, SettingsService.Settings.HardwareSyncApiUrl)
                {
                    Content = JsonContent.Create(data)
                };

                if (!string.IsNullOrWhiteSpace(SettingsService.Settings.HardwareSyncApiKey))
                {
                    request.Headers.TryAddWithoutValidation("X-Upload-Key", SettingsService.Settings.HardwareSyncApiKey);
                }

                using var response = await HttpClient.SendAsync(request);
                if (response.IsSuccessStatusCode)
                {
                    Logger.LogInformation("硬件数据同步成功。");
                    _lastUploadDate = DateTime.Today;
                }
                else
                {
                    Logger.LogWarning("硬件数据同步失败: {StatusCode}", response.StatusCode);
                }
            }
        }
        catch (Exception ex)
        {
            Logger.LogError(ex, "硬件数据同步时发生异常。");
        }
    }

    private bool _isBluetoothSyncing = false;

    private async Task SendViaBluetoothAsync(object data)
    {
        if (_isBluetoothSyncing) return;
        _isBluetoothSyncing = true;

        try
        {
            var ble = CrossBluetoothLE.Current;
            var adapter = CrossBluetoothLE.Current.Adapter;
            
            if (!ble.IsOn)
            {
                Logger.LogInformation("蓝牙未开启，跳过同步。");
                return;
            }

            Logger.LogInformation("正在扫描 ClassIsland_OLED 或 ClassIsland_TFT 蓝牙设备...");
            
            IDevice? targetDevice = null;
            
            adapter.DeviceDiscovered += (s, a) =>
            {
                if (a.Device.Name == "ClassIsland_OLED" || a.Device.Name == "ClassIsland_TFT")
                {
                    targetDevice = a.Device;
                    _ = adapter.StopScanningForDevicesAsync();
                }
            };

            var cts = new System.Threading.CancellationTokenSource(TimeSpan.FromSeconds(3));
            try
            {
                await adapter.StartScanningForDevicesAsync(new Plugin.BLE.Abstractions.ScanFilterOptions(), null, false, cts.Token);
            }
            catch (TaskCanceledException)
            {
                // 超时正常结束扫描
            }
            
            if (targetDevice == null)
            {
                var connectedDevices = adapter.ConnectedDevices;
                targetDevice = connectedDevices.FirstOrDefault(d => d.Name == "ClassIsland_OLED" || d.Name == "ClassIsland_TFT");
            }

            if (targetDevice != null)
            {
                Logger.LogInformation("找到 BLE 设备，尝试连接...");
                
                await adapter.ConnectToDeviceAsync(targetDevice);
                
                var serviceUuid = new Guid("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
                var charUuid = new Guid("beb5483e-36e1-4688-b7f5-ea07361b26a8");

                var service = await targetDevice.GetServiceAsync(serviceUuid);
                if (service != null)
                {
                    var characteristic = await service.GetCharacteristicAsync(charUuid);
                    if (characteristic != null)
                    {
                        var jsonBytes = JsonSerializer.SerializeToUtf8Bytes(data);
                        var payloadBytes = new byte[jsonBytes.Length + 1];
                        Array.Copy(jsonBytes, payloadBytes, jsonBytes.Length);
                        payloadBytes[payloadBytes.Length - 1] = (byte)'\n';

                        int chunkSize = 100;
                        int offset = 0;
                        bool allSuccess = true;
                        
                        while (offset < payloadBytes.Length)
                        {
                            int count = Math.Min(chunkSize, payloadBytes.Length - offset);
                            var chunk = new byte[count];
                            Array.Copy(payloadBytes, offset, chunk, 0, count);
                            
                            var writeResult = await characteristic.WriteAsync(chunk);
                            
                            offset += count;
                            await Task.Delay(20);
                        }

                        if (allSuccess)
                        {
                            Logger.LogInformation("通过蓝牙同步数据成功。");
                        }
                    }
                    else
                    {
                        Logger.LogWarning("无法找到蓝牙服务对应的特征值。");
                    }
                }
                else
                {
                    Logger.LogWarning("无法找到蓝牙设备的服务。");
                }
                
                await adapter.DisconnectDeviceAsync(targetDevice);
            }
            else
            {
                Logger.LogInformation("未发现名为 ClassIsland_OLED 或 ClassIsland_TFT 的蓝牙设备。");
            }
        }
        catch (DeviceConnectionException e)
        {
            Logger.LogWarning(e, "连接蓝牙设备失败");
        }
        catch (Exception ex)
        {
            Logger.LogError(ex, "蓝牙同步数据时发生异常。");
        }
        finally
        {
            _isBluetoothSyncing = false;
        }
    }
}
