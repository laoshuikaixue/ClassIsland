# 硬件上传鉴权说明

## 1) 服务器端配置

在部署 Flask 前设置环境变量：

```bash
HARDWARE_UPLOAD_KEY=请替换为你的秘钥
```

当 `HARDWARE_UPLOAD_KEY` 为空时，服务器不校验秘钥。  
当 `HARDWARE_UPLOAD_KEY` 有值时，`/api/upload` 必须携带请求头 `X-Upload-Key`，否则返回 `401`。

## 2) ClassIsland 端配置

在 `Settings.json` 中添加或修改：

```json
{
  "HardwareSyncApiUrl": "http://47.116.166.10:5000/api/upload",
  "HardwareSyncApiKey": "CIHW_2026_7f9bA2dE4kLm8QpR"
}
```

## 3) ESP32 端

`/api/course` 与 `/api/voicehub` 保持原逻辑，不需要上传秘钥。
