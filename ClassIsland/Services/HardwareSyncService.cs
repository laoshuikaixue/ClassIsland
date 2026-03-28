using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Json;
using System.Threading.Tasks;
using ClassIsland.Core.Abstractions.Services;
using ClassIsland.Shared.Models.Profile;
using Microsoft.Extensions.Logging;

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

        if (shouldSync)
        {
            _ = SyncDataAsync();
            _lastWeatherUpdateTime = SettingsService.Settings.LastWeatherInfo?.UpdateTime ?? DateTime.MinValue;
        }
    }

    public async Task SyncDataAsync()
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

            var data = new
            {
                date = DateTime.Today.ToString("yyyy-MM-dd"),
                weather = new
                {
                    text = weatherInfo != null ? WeatherService.GetWeatherTextByCode(weatherInfo.Current.Weather) : "未知",
                    temp = weatherInfo?.Current?.Temperature?.Value ?? "0",
                    rain = rainMsg
                },
                courses = courses
            };

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
        catch (Exception ex)
        {
            Logger.LogError(ex, "硬件数据同步时发生异常。");
        }
    }
}
