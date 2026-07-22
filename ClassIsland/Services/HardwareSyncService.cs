using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using ClassIsland.Core.Abstractions.Services;
using Microsoft.Extensions.Logging;
using Plugin.BLE;
using Plugin.BLE.Abstractions.Contracts;
using Plugin.BLE.Abstractions.Exceptions;

namespace ClassIsland.Services;

/// <summary>
/// 将课程信息同步到 Flask、ESP32 BLE 和 Dot. 设备。
/// </summary>
public class HardwareSyncService
{
    private const string DotBaseUrl = "https://dot.mindreset.tech";
    private const string DotTaskAlias = "ClassIsland Schedule";
    private static readonly TimeSpan DotDebounce = TimeSpan.FromSeconds(2);
    private static readonly TimeSpan[] DotRetryDelays =
    [
        TimeSpan.FromSeconds(30),
        TimeSpan.FromMinutes(2),
        TimeSpan.FromMinutes(5),
        TimeSpan.FromMinutes(15)
    ];
    private static readonly int[] DotCountdownThresholds = [30, 15, 5, 1];

    private static readonly JsonElement DotCanvasWindowData = JsonDocument.Parse("""
        {
          "default": [
            {
              "type": "div",
              "props": {
                "tw": "flex flex-col w-full h-full bg-white text-black p-[4px] gap-[2px] box-border overflow-hidden",
                "children": [
                  {
                    "type": "div",
                    "props": {
                      "tw": "flex flex-row items-center justify-between min-w-0 h-[18px]",
                      "children": [
                        {
                          "type": "div",
                          "props": {
                            "tw": "text-13-chillduansans font-semibold min-w-0",
                            "style": { "fontSize": "13px", "lineHeight": "16px", "whiteSpace": "nowrap" },
                            "children": "{{get inputData \"dateLine\" default=\"-\"}}"
                          }
                        },
                        {
                          "type": "div",
                          "props": {
                            "tw": "text-12-chillduansans min-w-0",
                            "style": { "fontSize": "12px", "lineHeight": "15px", "lineClamp": 1, "overflow": "hidden", "textOverflow": "ellipsis", "whiteSpace": "nowrap", "textAlign": "right" },
                            "children": "{{get inputData \"weather\" default=\"\"}}"
                          }
                        }
                      ]
                    }
                  },
                  {
                    "type": "div",
                    "props": {
                      "tw": "flex flex-row items-center min-w-0 h-[40px] border-t border-b border-black",
                      "children": [
                        {
                          "type": "div",
                          "props": {
                            "tw": "flex flex-col flex-1 min-w-0",
                            "children": [
                              {
                                "type": "div",
                                "props": {
                                  "tw": "text-10-chillduansans",
                                  "style": { "fontSize": "10px", "lineHeight": "11px" },
                                  "children": "{{get inputData \"state\" default=\"\"}}"
                                }
                              },
                              {
                                "type": "div",
                                "props": {
                                  "tw": "text-23-chillduansans font-bold min-w-0",
                                  "style": { "fontSize": "23px", "fontWeight": 700, "lineHeight": "25px", "lineClamp": 1, "overflow": "hidden", "textOverflow": "ellipsis", "whiteSpace": "nowrap" },
                                  "children": "{{get inputData \"title\" default=\"-\"}}"
                                }
                              }
                            ]
                          }
                        },
                        {
                          "type": "div",
                          "props": {
                            "tw": "text-13-chillduansans font-semibold shrink-0",
                            "style": { "fontSize": "13px", "fontWeight": 600, "lineHeight": "16px", "whiteSpace": "nowrap", "textAlign": "right" },
                            "children": "{{get inputData \"remaining\" default=\"\"}}"
                          }
                        }
                      ]
                    }
                  },
                  {
                    "type": "div",
                    "props": {
                      "tw": "text-13-chillduansans font-semibold min-w-0 h-[15px]",
                      "style": { "fontSize": "13px", "fontWeight": 600, "lineHeight": "15px", "lineClamp": 1, "overflow": "hidden", "textOverflow": "ellipsis", "whiteSpace": "nowrap" },
                      "children": "{{get inputData \"next\" default=\"\"}}"
                    }
                  },
                  {
                    "type": "div",
                    "props": {
                      "tw": "flex flex-col min-w-0 h-[48px] border-t border-black",
                      "children": [
                        {
                          "type": "div",
                          "props": {
                            "tw": "text-10-chillduansans font-semibold",
                            "style": { "fontSize": "10px", "fontWeight": 600, "lineHeight": "11px" },
                            "children": "{{get inputData \"scheduleTitle\" default=\"今日课表\"}}"
                          }
                        },
                        {
                          "type": "div",
                          "props": {
                            "tw": "text-11-chillduansans min-w-0",
                            "style": { "fontSize": "11px", "lineHeight": "12px", "whiteSpace": "nowrap", "overflow": "hidden", "textOverflow": "ellipsis" },
                            "children": "{{get inputData \"row1\" default=\"\"}}"
                          }
                        },
                        {
                          "type": "div",
                          "props": {
                            "tw": "text-11-chillduansans min-w-0",
                            "style": { "fontSize": "11px", "lineHeight": "12px", "whiteSpace": "nowrap", "overflow": "hidden", "textOverflow": "ellipsis" },
                            "children": "{{get inputData \"row2\" default=\"\"}}"
                          }
                        },
                        {
                          "type": "div",
                          "props": {
                            "tw": "text-11-chillduansans min-w-0",
                            "style": { "fontSize": "11px", "lineHeight": "12px", "whiteSpace": "nowrap", "overflow": "hidden", "textOverflow": "ellipsis" },
                            "children": "{{get inputData \"row3\" default=\"\"}}"
                          }
                        }
                      ]
                    }
                  },
                  {
                    "type": "div",
                    "props": {
                      "tw": "text-10-chillduansans min-w-0 h-[13px] border-t border-black",
                      "style": { "fontSize": "10px", "lineHeight": "12px", "lineClamp": 1, "overflow": "hidden", "textOverflow": "ellipsis", "whiteSpace": "nowrap" },
                      "children": "{{get inputData \"footer\" default=\"暂无近期广播排期\"}}"
                    }
                  }
                ]
              }
            }
          ]
        }
        """).RootElement.Clone();

    private static readonly JsonElement DotCanvasLayoutFull = JsonDocument.Parse("""
        {
          "tw": "p-0 bg-white",
          "style": { "padding": 0, "backgroundColor": "#FFFFFF" }
        }
        """).RootElement.Clone();

    private ILessonsService LessonsService { get; }
    private SettingsService SettingsService { get; }
    private IProfileService ProfileService { get; }
    private IWeatherService WeatherService { get; }
    private ILogger<HardwareSyncService> Logger { get; }
    private HttpClient HttpClient { get; } = new();
    private HttpClient DotHttpClient { get; } = new() { Timeout = TimeSpan.FromSeconds(15) };

    private readonly object _snapshotStateLock = new();
    private readonly object _dotStateLock = new();
    private readonly SemaphoreSlim _payloadBuildSemaphore = new(1, 1);
    private readonly SemaphoreSlim _dotRequestSemaphore = new(1, 1);
    private readonly SemaphoreSlim _dotPumpSignal = new(0, 1);
    private readonly HashSet<string> _activeDotFingerprints = [];
    private DateTime _lastUploadDate = DateTime.MinValue;
    private DateTime _lastWeatherUpdateTime = DateTime.MinValue;
    private DateTime _lastBluetoothSyncTime = DateTime.MinValue;
    private DateTimeOffset _lastDotEvaluationTime = DateTimeOffset.MinValue;
    private HardwareSyncPayload? _latestPayload;
    private DateTimeOffset _latestPayloadBuiltAt = DateTimeOffset.MinValue;
    private int _isNetworkSyncing;
    private int _isBluetoothSyncing;

    private DotSyncRequest? _pendingDotRequest;
    private bool _dotPumpRunning;
    private bool _dotConfigurationBlocked;
    private int _dotConfigurationRevision;
    private int _dotFailureCount;
    private DateTimeOffset _dotRetryAt = DateTimeOffset.MinValue;
    private string? _lastDotFingerprint;
    private string? _lastDotStateKey;
    private string? _dotCountdownStateKey;
    private int _dotCountdownLastDisplayedMinutes;
    private string _dotCountdownText = "";

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

        LessonsService.PostMainTimerTicked += LessonsServiceOnPostMainTimerTicked;
        SettingsService.Settings.PropertyChanged += SettingsOnPropertyChanged;
    }

    private void LessonsServiceOnPostMainTimerTicked(object? sender, EventArgs e)
    {
        if (!SettingsService.Settings.IsHardwareSyncEnabled)
            return;

        var now = DateTime.Now;
        EvaluateDotFromLatestSnapshot(now);

        var weatherUpdateTime = SettingsService.Settings.LastWeatherInfo?.UpdateTime ?? DateTime.MinValue;
        var shouldSync = DateTime.Today != _lastUploadDate ||
                         weatherUpdateTime != DateTime.MinValue && weatherUpdateTime != _lastWeatherUpdateTime;

        // 保留原有规则：蓝牙同步每 5 秒尝试一次。
        var shouldSyncBluetooth = (now - _lastBluetoothSyncTime).TotalSeconds >= 5;

        if (shouldSync && Interlocked.CompareExchange(ref _isNetworkSyncing, 1, 0) == 0)
            _ = RunScheduledNetworkSyncAsync(weatherUpdateTime);

        if (!shouldSyncBluetooth)
            return;

        _lastBluetoothSyncTime = now;
        _ = RunScheduledBluetoothSyncAsync();
    }

    private void SettingsOnPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is not (nameof(Models.Settings.IsHardwareSyncEnabled) or
            nameof(Models.Settings.IsDotHardwareSyncEnabled) or
            nameof(Models.Settings.DotHardwareSyncApiKey) or
            nameof(Models.Settings.DotHardwareSyncDeviceId) or
            nameof(Models.Settings.DotHardwareSyncTaskKey)))
            return;

        lock (_dotStateLock)
        {
            _dotConfigurationRevision++;
            _dotConfigurationBlocked = false;
            _dotFailureCount = 0;
            _dotRetryAt = DateTimeOffset.MinValue;
            _lastDotFingerprint = null;
            _lastDotStateKey = null;
            _pendingDotRequest = null;
            _dotCountdownStateKey = null;
            _dotCountdownText = "";
        }
        WakeDotPump();

        if (SettingsService.Settings.IsHardwareSyncEnabled && SettingsService.Settings.IsDotHardwareSyncEnabled)
            _ = RefreshDotAfterSettingsChangeAsync();
    }

    private async Task RefreshDotAfterSettingsChangeAsync()
    {
        try
        {
            var payload = await GetHardwarePayloadAsync(forceRefresh: true);
            if (payload != null)
                QueueDotSync(payload, immediate: true);
        }
        catch (Exception ex)
        {
            Logger.LogWarning(ex, "刷新 Dot 设置后的课表快照失败。");
        }
    }

    public async Task SyncDataAsync(bool syncNetwork = true, bool syncBluetooth = true)
    {
        try
        {
            var data = await GetHardwarePayloadAsync(forceRefresh: syncNetwork);
            if (data == null)
            {
                Logger.LogInformation("当前没有加载的课表，跳过硬件同步。");
                return;
            }

            // ESP32 的 payload 与发送规则保持不变。
            if (syncBluetooth)
                _ = SendViaBluetoothAsync(data);

            if (syncNetwork)
            {
                if (await SendNetworkPayloadAsync(data))
                {
                    _lastUploadDate = DateTime.Today;
                    _lastWeatherUpdateTime = SettingsService.Settings.LastWeatherInfo?.UpdateTime ?? DateTime.MinValue;
                }
            }

            QueueDotSync(data);
        }
        catch (Exception ex)
        {
            Logger.LogError(ex, "硬件数据同步时发生异常。");
        }
    }

    private async Task RunScheduledNetworkSyncAsync(DateTime weatherUpdateTime)
    {
        try
        {
            var data = await GetHardwarePayloadAsync(forceRefresh: true);
            if (data == null)
                return;

            QueueDotSync(data);
            if (!await SendNetworkPayloadAsync(data))
                return;

            _lastUploadDate = DateTime.Today;
            _lastWeatherUpdateTime = weatherUpdateTime;
        }
        catch (Exception ex)
        {
            Logger.LogError(ex, "硬件数据同步时发生异常。");
        }
        finally
        {
            Interlocked.Exchange(ref _isNetworkSyncing, 0);
        }
    }

    private async Task RunScheduledBluetoothSyncAsync()
    {
        try
        {
            var data = await GetHardwarePayloadAsync();
            if (data == null)
                return;

            QueueDotSync(data);
            await SendViaBluetoothAsync(data);
        }
        catch (Exception ex)
        {
            Logger.LogError(ex, "准备蓝牙硬件同步时发生异常。");
        }
    }

    private async Task<bool> SendNetworkPayloadAsync(HardwareSyncPayload data)
    {
        using var request = new HttpRequestMessage(HttpMethod.Post, SettingsService.Settings.HardwareSyncApiUrl)
        {
            Content = JsonContent.Create(data)
        };

        if (!string.IsNullOrWhiteSpace(SettingsService.Settings.HardwareSyncApiKey))
            request.Headers.TryAddWithoutValidation("X-Upload-Key", SettingsService.Settings.HardwareSyncApiKey);

        using var response = await HttpClient.SendAsync(request);
        if (response.IsSuccessStatusCode)
        {
            Logger.LogInformation("硬件数据同步成功。");
            return true;
        }

        Logger.LogWarning("硬件数据同步失败: {StatusCode}", response.StatusCode);
        return false;
    }

    private async Task<HardwareSyncPayload?> GetHardwarePayloadAsync(bool forceRefresh = false)
    {
        var requestedAt = DateTimeOffset.Now;
        await _payloadBuildSemaphore.WaitAsync();
        try
        {
            lock (_snapshotStateLock)
            {
                if (_latestPayload != null &&
                    (_latestPayloadBuiltAt >= requestedAt ||
                     !forceRefresh && requestedAt - _latestPayloadBuiltAt < TimeSpan.FromSeconds(1)))
                    return _latestPayload;
            }

            var payload = await BuildHardwarePayloadAsync();
            lock (_snapshotStateLock)
            {
                _latestPayload = payload;
                _latestPayloadBuiltAt = DateTimeOffset.Now;
            }
            return payload;
        }
        finally
        {
            _payloadBuildSemaphore.Release();
        }
    }

    private HardwareSyncPayload? GetLatestPayload()
    {
        lock (_snapshotStateLock)
            return _latestPayload;
    }

    private void EvaluateDotFromLatestSnapshot(DateTime now)
    {
        var timestamp = new DateTimeOffset(now);
        if (timestamp - _lastDotEvaluationTime < TimeSpan.FromMilliseconds(250))
            return;

        _lastDotEvaluationTime = timestamp;
        var payload = GetLatestPayload();
        if (payload != null)
            QueueDotSync(payload);
    }

    public async Task<HardwareSyncTestResult> TestDotSyncAsync()
    {
        var config = GetDotConfiguration();
        var validationMessage = ValidateDotConfiguration(config);
        if (validationMessage != null)
            return new HardwareSyncTestResult(false, validationMessage);

        var data = await GetHardwarePayloadAsync(forceRefresh: true);
        if (data == null)
            return new HardwareSyncTestResult(false, "当前没有加载的课表，无法生成测试画面。");

        int revision;
        lock (_dotStateLock)
        {
            _dotConfigurationRevision++;
            revision = _dotConfigurationRevision;
            _dotConfigurationBlocked = false;
            _dotFailureCount = 0;
            _dotRetryAt = DateTimeOffset.MinValue;
            _pendingDotRequest = null;
        }

        var request = BuildDotSyncRequest(data, config, revision, DateTimeOffset.MinValue);
        lock (_dotStateLock)
            _activeDotFingerprints.Add(request.Fingerprint);
        var outcome = await SendDotRequestAsync(request);
        ApplyDotOutcome(request, outcome, retryAutomatically: SettingsService.Settings.IsDotHardwareSyncEnabled);
        return new HardwareSyncTestResult(outcome.IsSuccess, outcome.Message);
    }

    private async Task<HardwareSyncPayload?> BuildHardwarePayloadAsync()
    {
        var plan = LessonsService.CurrentClassPlan;
        if (plan == null)
            return null;

        var courses = new List<HardwareCoursePayload>();
        foreach (var classInfo in plan.Classes)
        {
            if (!ProfileService.Profile.Subjects.TryGetValue(classInfo.SubjectId, out var subject))
                continue;

            var timeItem = classInfo.CurrentTimeLayoutItem;
            courses.Add(new HardwareCoursePayload
            {
                Name = subject.Name,
                StartTime = timeItem.StartTime.ToString(@"hh\:mm"),
                EndTime = timeItem.EndTime.ToString(@"hh\:mm")
            });
        }

        var weatherInfo = SettingsService.Settings.LastWeatherInfo;
        var rainMin = weatherInfo?.Minutely?.Precipitation?.RainRemainingMinutes ?? 0;
        var rainMessage = "";
        if (rainMin > 0)
        {
            rainMessage = $"预计 {Converters.MinutesToApproxTimeConverter.Instance.Convert(rainMin, typeof(string), null, CultureInfo.CurrentCulture)} 后下雨";
        }
        else if (rainMin < 0)
        {
            rainMessage = $"预计 {Converters.MinutesToApproxTimeConverter.Instance.Convert(-rainMin, typeof(string), null, CultureInfo.CurrentCulture)} 后雨停";
        }

        var tomorrowCourses = new List<HardwareCoursePayload>();
        var tomorrowPlan = LessonsService.GetClassPlanByDate(DateTime.Today.AddDays(1), out _);
        if (tomorrowPlan != null)
        {
            foreach (var classInfo in tomorrowPlan.Classes)
            {
                if (!ProfileService.Profile.Subjects.TryGetValue(classInfo.SubjectId, out var subject))
                    continue;

                var timeItem = classInfo.CurrentTimeLayoutItem;
                tomorrowCourses.Add(new HardwareCoursePayload
                {
                    Name = subject.Name,
                    StartTime = timeItem.StartTime.ToString(@"hh\:mm"),
                    EndTime = timeItem.EndTime.ToString(@"hh\:mm")
                });
            }
        }

        return new HardwareSyncPayload
        {
            Date = DateTime.Today.ToString("yyyy-MM-dd"),
            Timestamp = DateTimeOffset.Now.ToUnixTimeSeconds(),
            Weather = new HardwareWeatherPayload
            {
                Text = weatherInfo != null ? WeatherService.GetWeatherTextByCode(weatherInfo.Current.Weather) : "未知",
                Temperature = weatherInfo?.Current?.Temperature?.Value ?? "0",
                Rain = rainMessage,
                Warning = weatherInfo?.Alerts?.FirstOrDefault()?.Title ?? "",
                Alerts = weatherInfo?.Alerts?.Select(a => new HardwareAlertPayload
                {
                    Type = a.Type,
                    Level = a.Level,
                    Title = a.Title
                }).ToArray() ?? []
            },
            Courses = courses,
            TomorrowCourses = tomorrowCourses,
            VoiceHub = await GetVoiceHubTextAsync()
        };
    }

    private static async Task<string> GetVoiceHubTextAsync()
    {
        try
        {
            using var httpVoiceHub = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };
            var response = await httpVoiceHub.GetAsync("https://voicehub.lao-shui.top/api/songs/public");
            if (!response.IsSuccessStatusCode)
                return "";

            var json = await response.Content.ReadAsStringAsync();
            using var document = JsonDocument.Parse(json);
            if (document.RootElement.ValueKind != JsonValueKind.Array)
                return "";

            var today = DateTime.Today;
            var futureDates = new HashSet<string>();
            foreach (var item in document.RootElement.EnumerateArray())
            {
                if (!item.TryGetProperty("playDate", out var playDateElement))
                    continue;
                var playDate = playDateElement.GetString();
                if (string.IsNullOrEmpty(playDate) || playDate.Length < 10)
                    continue;
                var dateText = playDate[..10];
                if (DateTime.TryParse(dateText, out var date) && date.Date >= today)
                    futureDates.Add(dateText);
            }

            if (futureDates.Count == 0)
                return "";

            var targetDate = futureDates.OrderBy(x => x).First();
            var result = $"广播站排期 {targetDate}: ";
            var index = 1;
            var targetItems = document.RootElement.EnumerateArray()
                .Where(item => item.TryGetProperty("playDate", out var date) &&
                               date.GetString()?.StartsWith(targetDate) == true &&
                               item.TryGetProperty("song", out _))
                .OrderBy(item => item.TryGetProperty("sequence", out var sequence) ? sequence.GetInt32() : 999);

            foreach (var item in targetItems)
            {
                var song = item.GetProperty("song");
                var title = song.TryGetProperty("title", out var titleElement) ? titleElement.GetString() ?? "" : "";
                var artist = song.TryGetProperty("artist", out var artistElement) ? artistElement.GetString() ?? "" : "";
                var requester = song.TryGetProperty("requester", out var requesterElement) ? requesterElement.GetString() ?? "" : "";
                result += $"#{index} {title}-{artist}";
                if (!string.IsNullOrEmpty(requester))
                    result += $"-{requester}";
                result += "  ";
                index++;
            }

            return result;
        }
        catch (Exception)
        {
            return "";
        }
    }

    private void QueueDotSync(HardwareSyncPayload data, bool immediate = false)
    {
        if (!SettingsService.Settings.IsHardwareSyncEnabled || !SettingsService.Settings.IsDotHardwareSyncEnabled)
            return;

        var config = GetDotConfiguration();
        if (ValidateDotConfiguration(config) != null)
            return;

        lock (_dotStateLock)
        {
            var now = DateTimeOffset.Now;
            var request = BuildDotSyncRequest(data, config, _dotConfigurationRevision, now);
            if (request.Fingerprint == _lastDotFingerprint || _activeDotFingerprints.Contains(request.Fingerprint))
                return;

            if (request.Fingerprint == _pendingDotRequest?.Fingerprint)
            {
                if (immediate && _pendingDotRequest.NotBefore > now)
                {
                    _pendingDotRequest = _pendingDotRequest with { NotBefore = now };
                    WakeDotPump();
                }
                return;
            }

            var isStateTransition = _lastDotStateKey == null || request.StateKey != _lastDotStateKey;
            request = request with { NotBefore = immediate || isStateTransition ? now : now + DotDebounce };

            _pendingDotRequest = request;
            if (_dotConfigurationBlocked)
                return;
            if (_dotPumpRunning)
            {
                WakeDotPump();
                return;
            }

            _dotPumpRunning = true;
            _ = Task.Run(RunDotPumpAsync);
        }
    }

    private async Task RunDotPumpAsync()
    {
        while (true)
        {
            DotSyncRequest? request = null;
            TimeSpan delay;
            lock (_dotStateLock)
            {
                if (_dotConfigurationBlocked || _pendingDotRequest == null)
                {
                    _dotPumpRunning = false;
                    return;
                }

                var now = DateTimeOffset.Now;
                var notBefore = _pendingDotRequest.NotBefore > _dotRetryAt ? _pendingDotRequest.NotBefore : _dotRetryAt;
                delay = notBefore > now ? notBefore - now : TimeSpan.Zero;
                if (delay <= TimeSpan.Zero)
                {
                    request = _pendingDotRequest;
                    _pendingDotRequest = null;
                    _activeDotFingerprints.Add(request.Fingerprint);
                }
            }

            if (request == null)
            {
                await _dotPumpSignal.WaitAsync(delay);
                continue;
            }

            var outcome = await SendDotRequestAsync(request);
            ApplyDotOutcome(request, outcome, retryAutomatically: true);
        }
    }

    private void WakeDotPump()
    {
        try
        {
            _dotPumpSignal.Release();
        }
        catch (SemaphoreFullException)
        {
            // 已有唤醒信号，无需重复排队。
        }
    }

    private async Task<DotSendOutcome> SendDotRequestAsync(DotSyncRequest request)
    {
        await _dotRequestSemaphore.WaitAsync();
        try
        {
            using var httpRequest = new HttpRequestMessage(
                HttpMethod.Post,
                $"{DotBaseUrl}/api/authV2/open/device/{Uri.EscapeDataString(request.Configuration.DeviceId)}/canvas");
            httpRequest.Headers.Authorization = new AuthenticationHeaderValue("Bearer", request.Configuration.ApiKey);

            var body = new Dictionary<string, object?>
            {
                ["refreshNow"] = true,
                ["taskAlias"] = DotTaskAlias,
                ["data"] = request.Data,
                ["windowData"] = DotCanvasWindowData,
                ["layoutFull"] = DotCanvasLayoutFull,
                ["border"] = 0
            };
            if (!string.IsNullOrWhiteSpace(request.Configuration.TaskKey))
                body["taskKey"] = request.Configuration.TaskKey;

            if (JsonSerializer.SerializeToUtf8Bytes(request.Data).Length > 64 * 1024)
                return new DotSendOutcome(false, false, null, "Dot Canvas 数据超过 64KB，已取消发送。");

            httpRequest.Content = JsonContent.Create(body);
            using var response = await DotHttpClient.SendAsync(httpRequest);
            if (response.IsSuccessStatusCode)
                return new DotSendOutcome(true, false, null, "Dot 当前课表已推送。");

            if (response.StatusCode is HttpStatusCode.Unauthorized or HttpStatusCode.Forbidden)
                return new DotSendOutcome(false, true, null, $"Dot API 拒绝访问（HTTP {(int)response.StatusCode}），请检查 API Key。");
            if (response.StatusCode == HttpStatusCode.NotFound)
                return new DotSendOutcome(false, true, null, "Dot 返回 HTTP 404，请检查设备 ID，并确认循环任务中已添加 Canvas API 内容。");
            if ((int)response.StatusCode == 429)
                return new DotSendOutcome(false, false, GetRetryAfter(response), "Dot API 请求过于频繁，已按服务端要求延后重试。");

            return new DotSendOutcome(false, false, null, $"Dot Canvas 请求失败（HTTP {(int)response.StatusCode}）。");
        }
        catch (TaskCanceledException)
        {
            return new DotSendOutcome(false, false, null, "Dot Canvas 请求超时。");
        }
        catch (HttpRequestException)
        {
            return new DotSendOutcome(false, false, null, "无法连接 Dot OpenAPI。");
        }
        catch (Exception)
        {
            return new DotSendOutcome(false, false, null, "Dot Canvas 请求发生未知错误。");
        }
        finally
        {
            _dotRequestSemaphore.Release();
        }
    }

    private void ApplyDotOutcome(DotSyncRequest request, DotSendOutcome outcome, bool retryAutomatically)
    {
        lock (_dotStateLock)
        {
            _activeDotFingerprints.Remove(request.Fingerprint);
            if (request.ConfigurationRevision != _dotConfigurationRevision)
                return;

            if (outcome.IsSuccess)
            {
                _lastDotFingerprint = request.Fingerprint;
                _lastDotStateKey = request.StateKey;
                _dotFailureCount = 0;
                _dotRetryAt = DateTimeOffset.MinValue;
                Logger.LogInformation("Dot 当前课表同步成功。");
                return;
            }

            if (outcome.IsConfigurationError)
            {
                _dotConfigurationBlocked = true;
                Logger.LogWarning("{Message} 修改 Dot 设置或手动测试后将重新尝试。", outcome.Message);
                return;
            }

            var fallbackDelay = DotRetryDelays[Math.Min(_dotFailureCount, DotRetryDelays.Length - 1)];
            _dotFailureCount++;
            var retryDelay = outcome.RetryAfter is { } requestedDelay && requestedDelay > TimeSpan.Zero
                ? requestedDelay
                : fallbackDelay;
            _dotRetryAt = DateTimeOffset.Now + retryDelay;
            if (retryAutomatically && SettingsService.Settings.IsDotHardwareSyncEnabled &&
                (_pendingDotRequest == null || _pendingDotRequest.Fingerprint == request.Fingerprint))
            {
                _pendingDotRequest = request with { NotBefore = _dotRetryAt };
            }
            if (_pendingDotRequest != null && !_dotPumpRunning && !_dotConfigurationBlocked)
            {
                _dotPumpRunning = true;
                _ = Task.Run(RunDotPumpAsync);
            }
            Logger.LogWarning("{Message} 将在 {Delay} 后重试。", outcome.Message, retryDelay);
        }
    }

    private DotSyncRequest BuildDotSyncRequest(
        HardwareSyncPayload payload,
        DotConfiguration configuration,
        int configurationRevision,
        DateTimeOffset notBefore)
    {
        var data = BuildDotCanvasData(payload, DateTime.Now);
        var fingerprint = string.Join('\u001f',
            configurationRevision.ToString(CultureInfo.InvariantCulture),
            configuration.DeviceId,
            configuration.TaskKey,
            JsonSerializer.Serialize(data));
        return new DotSyncRequest(data, data.StateKey, configuration, configurationRevision, fingerprint, notBefore);
    }

    private DotCanvasData BuildDotCanvasData(HardwareSyncPayload payload, DateTime now)
    {
        var today = ToCourseViews(payload.Courses);
        var tomorrow = ToCourseViews(payload.TomorrowCourses);
        var currentTime = now.TimeOfDay;
        var currentIndex = today.FindIndex(course => currentTime >= course.Start && currentTime < course.End);
        var nextIndex = today.FindIndex(course => course.Start > currentTime);

        var state = "";
        var title = "";
        var remaining = "";
        var next = "";
        var referenceIndex = 0;
        var useTomorrowRows = false;
        string countdownStateKey;

        if (today.Count == 0)
        {
            state = "今日无课";
            title = "今天没有课程";
            next = tomorrow.Count > 0 ? $"明日 {tomorrow[0].Start:hh\\:mm}  {tomorrow[0].Name}" : "明日暂无课程";
            useTomorrowRows = true;
            countdownStateKey = $"{payload.Date}:no-classes";
        }
        else if (currentIndex >= 0)
        {
            var current = today[currentIndex];
            state = "上课中";
            title = current.Name;
            remaining = GetAdaptiveCountdownText(
                $"{payload.Date}:class:{currentIndex}:{current.Start}",
                MinutesUntil(current.End, currentTime),
                "剩余 ",
                "即将下课");
            next = currentIndex + 1 < today.Count
                ? $"下一节 {today[currentIndex + 1].Start:hh\\:mm}  {today[currentIndex + 1].Name}"
                : "本日最后一节课";
            referenceIndex = currentIndex;
            countdownStateKey = $"{payload.Date}:class:{currentIndex}:{current.Start}";
        }
        else if (nextIndex >= 0)
        {
            var upcoming = today[nextIndex];
            var isBeforeSchool = nextIndex == 0;
            state = isBeforeSchool ? "未上课" : "课间";
            title = upcoming.Name;
            remaining = GetAdaptiveCountdownText(
                $"{payload.Date}:{state}:{nextIndex}:{upcoming.Start}",
                MinutesUntil(upcoming.Start, currentTime),
                "距上课 ",
                "即将上课");
            next = $"{upcoming.Start:hh\\:mm} 开始 · {upcoming.End:hh\\:mm} 结束";
            referenceIndex = nextIndex;
            countdownStateKey = $"{payload.Date}:{state}:{nextIndex}:{upcoming.Start}";
        }
        else
        {
            state = "已放学";
            title = "今日课程结束";
            next = tomorrow.Count > 0 ? $"明日 {tomorrow[0].Start:hh\\:mm}  {tomorrow[0].Name}" : "明日暂无课程";
            useTomorrowRows = true;
            countdownStateKey = $"{payload.Date}:after-school";
        }

        if (string.IsNullOrEmpty(remaining))
            SetCountdownState(countdownStateKey);

        var rows = useTomorrowRows ? tomorrow : today;
        var firstRowIndex = useTomorrowRows ? 0 : Math.Max(0, Math.Min(referenceIndex - 1, Math.Max(0, rows.Count - 3)));
        var rowTexts = new string[3];
        for (var i = 0; i < rowTexts.Length; i++)
        {
            var index = firstRowIndex + i;
            if (index >= rows.Count)
            {
                rowTexts[i] = "";
                continue;
            }

            var marker = !useTomorrowRows && index == currentIndex ? "● " : "  ";
            rowTexts[i] = TruncateText($"{marker}{rows[index].Start:hh\\:mm}  {rows[index].Name}", 24);
        }

        var weatherDetail = !string.IsNullOrWhiteSpace(payload.Weather.Warning)
            ? payload.Weather.Warning
            : payload.Weather.Rain;
        var weather = $"{payload.Weather.Text} {payload.Weather.Temperature}℃";
        if (!string.IsNullOrWhiteSpace(weatherDetail))
            weather += $" · {weatherDetail}";

        return new DotCanvasData
        {
            StateKey = countdownStateKey,
            DateLine = $"{now:M月d日} {GetChineseWeekday(now.DayOfWeek)}",
            Weather = TruncateText(weather, 28),
            State = state,
            Title = TruncateText(title, 16),
            Remaining = remaining,
            Next = TruncateText(next, 28),
            ScheduleTitle = useTomorrowRows ? "明日课表" : "今日课表",
            Row1 = rowTexts[0],
            Row2 = rowTexts[1],
            Row3 = rowTexts[2],
            Footer = TruncateText(string.IsNullOrWhiteSpace(payload.VoiceHub) ? "暂无近期广播排期" : payload.VoiceHub, 52)
        };
    }

    private string GetAdaptiveCountdownText(string stateKey, int remainingMinutes, string prefix, string finalText)
    {
        lock (_dotStateLock)
        {
            if (_dotCountdownStateKey != stateKey)
            {
                _dotCountdownStateKey = stateKey;
                _dotCountdownLastDisplayedMinutes = remainingMinutes;
                _dotCountdownText = remainingMinutes <= 1 ? finalText : $"{prefix}{remainingMinutes} 分钟";
                return _dotCountdownText;
            }

            var crossedThreshold = DotCountdownThresholds
                .Where(threshold => remainingMinutes <= threshold && _dotCountdownLastDisplayedMinutes > threshold)
                .LastOrDefault();
            if (crossedThreshold > 0)
            {
                _dotCountdownLastDisplayedMinutes = crossedThreshold;
                _dotCountdownText = crossedThreshold == 1 ? finalText : $"{prefix}{crossedThreshold} 分钟";
            }

            return _dotCountdownText;
        }
    }

    private void SetCountdownState(string stateKey)
    {
        lock (_dotStateLock)
        {
            if (_dotCountdownStateKey == stateKey)
                return;
            _dotCountdownStateKey = stateKey;
            _dotCountdownLastDisplayedMinutes = 0;
            _dotCountdownText = "";
        }
    }

    private static List<CourseView> ToCourseViews(IEnumerable<HardwareCoursePayload> courses)
    {
        var result = new List<CourseView>();
        foreach (var course in courses)
        {
            if (!TimeSpan.TryParseExact(course.StartTime, @"hh\:mm", CultureInfo.InvariantCulture, out var start) ||
                !TimeSpan.TryParseExact(course.EndTime, @"hh\:mm", CultureInfo.InvariantCulture, out var end))
                continue;
            result.Add(new CourseView(course.Name, start, end));
        }
        return result.OrderBy(course => course.Start).ToList();
    }

    private static int MinutesUntil(TimeSpan target, TimeSpan current) =>
        Math.Max(1, (int)Math.Ceiling((target - current).TotalMinutes));

    private static string TruncateText(string text, int maxLength) =>
        text.Length <= maxLength ? text : $"{text[..Math.Max(0, maxLength - 1)]}…";

    private static string GetChineseWeekday(DayOfWeek day) => day switch
    {
        DayOfWeek.Monday => "周一",
        DayOfWeek.Tuesday => "周二",
        DayOfWeek.Wednesday => "周三",
        DayOfWeek.Thursday => "周四",
        DayOfWeek.Friday => "周五",
        DayOfWeek.Saturday => "周六",
        _ => "周日"
    };

    private DotConfiguration GetDotConfiguration() => new(
        SettingsService.Settings.DotHardwareSyncApiKey.Trim(),
        SettingsService.Settings.DotHardwareSyncDeviceId.Trim(),
        SettingsService.Settings.DotHardwareSyncTaskKey.Trim());

    private static string? ValidateDotConfiguration(DotConfiguration configuration)
    {
        if (string.IsNullOrWhiteSpace(configuration.ApiKey))
            return "请填写 Dot API Key。";
        if (!configuration.ApiKey.StartsWith("dot_app_", StringComparison.Ordinal))
            return "Dot API Key 格式无效，应以 dot_app_ 开头。";
        if (string.IsNullOrWhiteSpace(configuration.DeviceId))
            return "请填写 Dot 设备 ID。";
        return null;
    }

    private static TimeSpan? GetRetryAfter(HttpResponseMessage response)
    {
        if (response.Headers.RetryAfter?.Delta is { } delta)
            return delta;
        if (response.Headers.RetryAfter?.Date is { } date)
        {
            var delay = date - DateTimeOffset.Now;
            return delay > TimeSpan.Zero ? delay : TimeSpan.Zero;
        }
        return null;
    }

    private async Task SendViaBluetoothAsync(object data)
    {
        if (Interlocked.CompareExchange(ref _isBluetoothSyncing, 1, 0) != 0)
            return;

        try
        {
            var ble = CrossBluetoothLE.Current;
            var adapter = CrossBluetoothLE.Current.Adapter;
            if (!ble.IsOn)
                return;

            IDevice? targetDevice = null;
            adapter.DeviceDiscovered += (_, args) =>
            {
                if (args.Device.Name is "ClassIsland_OLED" or "ClassIsland_TFT")
                {
                    targetDevice = args.Device;
                    _ = adapter.StopScanningForDevicesAsync();
                }
            };

            using var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(3));
            try
            {
                await adapter.StartScanningForDevicesAsync(new Plugin.BLE.Abstractions.ScanFilterOptions(), null, false, cancellation.Token);
            }
            catch (TaskCanceledException)
            {
                // 扫描超时属于正常情况。
            }

            targetDevice ??= adapter.ConnectedDevices.FirstOrDefault(device =>
                device.Name is "ClassIsland_OLED" or "ClassIsland_TFT");
            if (targetDevice == null)
                return;

            Logger.LogInformation("找到 BLE 设备，尝试连接...");
            await adapter.ConnectToDeviceAsync(targetDevice);

            var service = await targetDevice.GetServiceAsync(new Guid("4fafc201-1fb5-459e-8fcc-c5c9c331914b"));
            if (service == null)
            {
                Logger.LogWarning("无法找到蓝牙设备的服务。");
                await adapter.DisconnectDeviceAsync(targetDevice);
                return;
            }

            var characteristic = await service.GetCharacteristicAsync(new Guid("beb5483e-36e1-4688-b7f5-ea07361b26a8"));
            if (characteristic == null)
            {
                Logger.LogWarning("无法找到蓝牙服务对应的特征值。");
                await adapter.DisconnectDeviceAsync(targetDevice);
                return;
            }

            var jsonBytes = JsonSerializer.SerializeToUtf8Bytes(data);
            var payloadBytes = new byte[jsonBytes.Length + 1];
            Array.Copy(jsonBytes, payloadBytes, jsonBytes.Length);
            payloadBytes[^1] = (byte)'\n';

            const int chunkSize = 100;
            for (var offset = 0; offset < payloadBytes.Length; offset += chunkSize)
            {
                var count = Math.Min(chunkSize, payloadBytes.Length - offset);
                var chunk = new byte[count];
                Array.Copy(payloadBytes, offset, chunk, 0, count);
                await characteristic.WriteAsync(chunk);
                await Task.Delay(20);
            }

            Logger.LogInformation("通过蓝牙同步数据成功。");
            await adapter.DisconnectDeviceAsync(targetDevice);
        }
        catch (DeviceConnectionException)
        {
            // 保留原有静默行为，避免 BLE 不可用时刷屏。
        }
        catch (Exception)
        {
            // 保留原有静默行为，Dot 和网络同步不受 BLE 故障影响。
        }
        finally
        {
            Interlocked.Exchange(ref _isBluetoothSyncing, 0);
        }
    }

    public sealed record HardwareSyncTestResult(bool IsSuccess, string Message);

    private sealed record DotConfiguration(string ApiKey, string DeviceId, string TaskKey);
    private sealed record DotSyncRequest(
        DotCanvasData Data,
        string StateKey,
        DotConfiguration Configuration,
        int ConfigurationRevision,
        string Fingerprint,
        DateTimeOffset NotBefore);
    private sealed record DotSendOutcome(
        bool IsSuccess,
        bool IsConfigurationError,
        TimeSpan? RetryAfter,
        string Message);
    private sealed record CourseView(string Name, TimeSpan Start, TimeSpan End);

    private sealed class HardwareSyncPayload
    {
        [JsonPropertyName("date")] public string Date { get; init; } = "";
        [JsonPropertyName("timestamp")] public long Timestamp { get; init; }
        [JsonPropertyName("weather")] public HardwareWeatherPayload Weather { get; init; } = new();
        [JsonPropertyName("courses")] public IReadOnlyList<HardwareCoursePayload> Courses { get; init; } = [];
        [JsonPropertyName("tomorrowCourses")] public IReadOnlyList<HardwareCoursePayload> TomorrowCourses { get; init; } = [];
        [JsonPropertyName("voiceHub")] public string VoiceHub { get; init; } = "";
    }

    private sealed class HardwareCoursePayload
    {
        [JsonPropertyName("name")] public string Name { get; init; } = "";
        [JsonPropertyName("startTime")] public string StartTime { get; init; } = "";
        [JsonPropertyName("endTime")] public string EndTime { get; init; } = "";
    }

    private sealed class HardwareWeatherPayload
    {
        [JsonPropertyName("text")] public string Text { get; init; } = "";
        [JsonPropertyName("temp")] public string Temperature { get; init; } = "";
        [JsonPropertyName("rain")] public string Rain { get; init; } = "";
        [JsonPropertyName("warning")] public string Warning { get; init; } = "";
        [JsonPropertyName("alerts")] public IReadOnlyList<HardwareAlertPayload> Alerts { get; init; } = [];
    }

    private sealed class HardwareAlertPayload
    {
        [JsonPropertyName("type")] public string Type { get; init; } = "";
        [JsonPropertyName("level")] public string Level { get; init; } = "";
        [JsonPropertyName("title")] public string Title { get; init; } = "";
    }

    private sealed class DotCanvasData
    {
        [JsonIgnore] public string StateKey { get; init; } = "";
        [JsonPropertyName("dateLine")] public string DateLine { get; init; } = "";
        [JsonPropertyName("weather")] public string Weather { get; init; } = "";
        [JsonPropertyName("state")] public string State { get; init; } = "";
        [JsonPropertyName("title")] public string Title { get; init; } = "";
        [JsonPropertyName("remaining")] public string Remaining { get; init; } = "";
        [JsonPropertyName("next")] public string Next { get; init; } = "";
        [JsonPropertyName("scheduleTitle")] public string ScheduleTitle { get; init; } = "";
        [JsonPropertyName("row1")] public string Row1 { get; init; } = "";
        [JsonPropertyName("row2")] public string Row2 { get; init; } = "";
        [JsonPropertyName("row3")] public string Row3 { get; init; } = "";
        [JsonPropertyName("footer")] public string Footer { get; init; } = "";
    }
}
