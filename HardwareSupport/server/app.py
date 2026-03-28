from flask import Flask, request, jsonify
import json
import os
import requests
import hmac
from datetime import datetime

app = Flask(__name__)

# 数据存储文件路径
DATA_FILE = 'course_data.json'
VOICEHUB_API = "https://voicehub.lao-shui.top/api/songs/public"
UPLOAD_KEY = os.getenv("HARDWARE_UPLOAD_KEY", "CIHW_2026_7f9bA2dE4kLm8QpR")

def save_data(data):
    with open(DATA_FILE, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=4)

def load_data():
    if os.path.exists(DATA_FILE):
        with open(DATA_FILE, 'r', encoding='utf-8') as f:
            return json.load(f)
    return {"courses": []}

def verify_upload_key():
    if not UPLOAD_KEY:
        return True
    request_key = request.headers.get("X-Upload-Key", "")
    return hmac.compare_digest(request_key, UPLOAD_KEY)

@app.route('/api/upload', methods=['POST'])
def upload_course():
    """
    接收 ClassIsland 上传的课程数据
    """
    if not verify_upload_key():
        return jsonify({"status": "error", "message": "Unauthorized"}), 401

    data = request.json
    if not data:
        return jsonify({"status": "error", "message": "No data received"}), 400
    
    save_data(data)
    return jsonify({"status": "success", "message": "Data uploaded successfully"})

@app.route('/api/course', methods=['GET'])
def get_course():
    """
    供 ESP32 拉取的课程数据接口
    """
    data = load_data()
    return jsonify(data)

@app.route('/api/voicehub', methods=['GET'])
def get_voicehub():
    """
    代理获取 VoiceHub 数据，过滤出今天的排期，减轻 ESP32 负担
    """
    try:
        response = requests.get(VOICEHUB_API, timeout=10)
        response.raise_for_status()
        songs_data = response.json()
        
        today_str = datetime.now().strftime("%Y-%m-%d")
        
        # 调试：打印获取到的数据类型和前几个元素的日期
        print(f"Fetched {len(songs_data)} items from VoiceHub.")
        
        target_date = None
        target_schedule = []
        
        if isinstance(songs_data, list):
            # 提取所有不早于今天的排期日期并排序
            future_dates = set()
            for item in songs_data:
                play_date = item.get("playDate", "")
                if play_date and play_date >= today_str:
                    # 取前10位(yyyy-MM-dd)
                    future_dates.add(play_date[:10])
            
            if future_dates:
                # 找到距离今天最近的一个日期
                target_date = sorted(list(future_dates))[0]
                
                # 提取该日期的所有排期
                for item in songs_data:
                    play_date = item.get("playDate", "")
                    if play_date and play_date.startswith(target_date) and "song" in item:
                        song_info = item["song"]
                        target_schedule.append({
                            "title": song_info.get("title", ""),
                            "artist": song_info.get("artist", ""),
                            "requester": song_info.get("requester", "")
                        })

        if target_date:
            print(f"Filtered {len(target_schedule)} items for nearest date ({target_date}).")
            return jsonify({
                "status": "success", 
                "targetDate": target_date,
                "data": target_schedule
            })
        else:
            print("No future schedule found.")
            return jsonify({
                "status": "success",
                "targetDate": None,
                "data": []
            })
        
    except Exception as e:
        print(f"Error fetching VoiceHub: {e}")
        return jsonify({"status": "error", "message": str(e), "data": []}), 500

if __name__ == '__main__':
    # 允许局域网访问
    debug_mode = os.getenv("FLASK_DEBUG", "0") == "1"
    app.run(host='0.0.0.0', port=5000, debug=debug_mode)
