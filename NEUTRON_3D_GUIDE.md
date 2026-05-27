# Hướng Dẫn Kiểm Tra & Xây Dựng Module 3D cho **react-force-graph (3D-Force-Graph)**

## 1. Tổng quan
Bản tài liệu này mô tả cách kiểm tra các module 3D, xây dựng một ví dụ **React 3D Force Graph** và thiết kế cơ chế tổng thể cho các *node* (Telegram, tư duy, thiết bị, v.v.).
Mục tiêu:
- Cung cấp **công cụ giao diện 3D** để người dùng dễ dàng định nghĩa, kết nối và chạy các tác vụ.
- Đảm bảo **tương thích với ESP32‑S3** khi xử lý ở biên bằng **Lua script**.
- Đưa ra các **tùy chọn tối tân** (engine, layout, style) giúp người dùng tùy biến nhanh.

---
## 2. Yêu cầu môi trường
| Thành phần | Phiên bản đề xuất |
|------------|-------------------|
| Node.js | `>=18.0.0` |
| npm | `>=9` |
| React | `18.x` |
| **3D‑Force‑Graph** | `^1.71.0` |
| **@react-three/fiber** | `^8.14.0` |
| **three** | `^0.165.0` |
| **d3-force-3d** | `^3.0.1` |
| ESP32‑S3 firmware | ESP‑IDF `v5.3.2` + **Lua 5.4** (via **elua** hoặc **node‑lua**) |
| MQTT broker | HiveMQ Cloud (TLS) |
| Supabase / PostgreSQL | Phiên bản hiện tại của dự án |

---
## 3. Cài đặt và khởi chạy ví dụ React 3D‑Force‑Graph
```bash
# Di chuyển vào thư mục front‑end (neutron-web)
cd "/home/nowh/ai_lab/new model/espclaw/neutron-web"
# Cài đặt phụ thuộc (nếu chưa có)
npm install react-force-graph-3d @react-three/fiber three d3-force-3d
# Thêm component mẫu
mkdir -p src/components/NeutronGraph
cat > src/components/NeutronGraph/NeutronGraph.tsx <<'EOF'
import React, { useEffect, useRef } from 'react';
import ForceGraph3D from 'react-force-graph-3d';
import * as THREE from 'three';

// Dữ liệu mẫu – sẽ được thay thế bằng API Supabase
const sampleData = {
  nodes: [
    { id: 'telegram', name: 'Telegram Bot', type: 'service' },
    { id: 'think', name: 'Tư duy', type: 'logic' },
    { id: 'device', name: 'ESP32‑S3', type: 'device' },
    { id: 'cloud', name: 'Cloud', type: 'cloud' },
  ],
  links: [
    { source: 'telegram', target: 'device', label: 'command' },
    { source: 'think', target: 'device', label: 'task' },
    { source: 'device', target: 'cloud', label: 'telemetry' },
  ],
};

export const NeutronGraph: React.FC = () => {
  const fgRef = useRef<any>(null);

  // Tùy chỉnh vật liệu 3D (đổ bóng, màu, v.v.)
  const nodeThreeObject = (node: any) => {
    const sprite = new THREE.Sprite(
      new THREE.SpriteMaterial({
        color: node.type === 'device' ? 0x00ff00 : node.type === 'service' ? 0x0088ff : 0xffaa00,
        depthWrite: false,
      })
    );
    sprite.scale.set(12, 12, 1);
    return sprite;
  };

  // Custom link label renderer (với Three.js CanvasTexture)
  const linkThreeObject = (link: any) => {
    const canvas = document.createElement('canvas');
    const ctx = canvas.getContext('2d')!;
    canvas.width = 256;
    canvas.height = 64;
    ctx.font = '24px sans-serif';
    ctx.fillStyle = '#fff';
    ctx.fillText(link.label, 10, 40);
    const texture = new THREE.CanvasTexture(canvas);
    const material = new THREE.SpriteMaterial({ map: texture, transparent: true });
    return new THREE.Sprite(material);
  };

  useEffect(() => {
    // Khi dữ liệu thay đổi – ví dụ fetch từ API – bạn có thể gọi fgRef.current.d3Force... để cập nhật lực
    const fg = fgRef.current;
    if (fg) {
      fg.d3Force('charge').strength(-200);
      fg.d3Force('link').distance(120).strength(0.8);
    }
  }, []);

  return (
    <ForceGraph3D
      ref={fgRef}
      graphData={sampleData}
      nodeAutoColorBy="type"
      nodeThreeObject={nodeThreeObject}
      linkThreeObject={linkThreeObject}
      linkDirectionalParticles={2}
      linkDirectionalParticleWidth={2}
      linkDirectionalParticleSpeed={0.005}
      onNodeClick={(node) => console.log('Node clicked:', node)}
      onLinkClick={(link) => console.log('Link clicked:', link)}
    />
  );
};
EOF

# Thêm component vào trang (ví dụ pages/neutron.tsx)
cat > pages/neutron.tsx <<'EOF'
import { NeutronGraph } from '@/components/NeutronGraph/NeutronGraph';
export default function NeutronPage() {
  return (
    <div style={{ width: '100vw', height: '100vh' }}>
      <NeutronGraph />
    </div>
  );
}
EOF

# Khởi chạy dev server (có thể đã đang chạy)
npm run dev &
```

Sau khi truy cập `http://localhost:3001/neutron` bạn sẽ thấy một đồ thị 3D hiển thị các node trên.

---
## 4. Kiểm tra các **module 3D**
| Module | Kiểm thử | Kết quả mong đợi |
|--------|----------|-------------------|
| `react-force-graph-3d` | Render 5 node mẫu | Đồ thị xoay trơn tru, label hiển thị |
| `@react-three/fiber` | Thêm custom sprite | Sprite hiển thị đúng màu và kích thước |
| `d3-force-3d` | Thay đổi `charge` & `link.distance` | Các node tự động phân tán/khốp lại theo lực |
| `three` | Canvas texture cho link label | Text label hiển thị dọc theo đường link |
| **WebSocket** (socket.io) | Nhận cập nhật dữ liệu realtime | Đồ thị cập nhật mà không reload page |

---
## 5. Cơ chế tổng thể – Kiểu dữ liệu *Node* và *Task*
### 5.1 Định dạng JSON cho **node**
```json
{
  "id": "telegram",
  "name": "Telegram Bot",
  "type": "service",   // service | logic | device | cloud
  "icon": "mdi:telegram",
  "meta": {
    "description": "Nhận lệnh từ người dùng và chuyển qua MQTT",
    "config": {
      "token": "YOUR_TELEGRAM_TOKEN",
      "allowed_chat_ids": [123456789]
    }
  }
}
```
### 5.2 Định dạng JSON cho **task** (tác vụ)
```json
{
  "task_id": "t001",
  "source": "telegram",
  "target": "device",
  "action": "read_data",
  "payload": {
    "path": "/sensors/temp",
    "interval": 5000
  },
  "schedule": "cron@*/5 * * * *",
  "retry": 3
}
```
*Các task* sẽ được lưu vào **Supabase** → bảng `neutron_tasks`. Backend sẽ chuyển task sang MQTT topic `espclaw/{device_id}/task`.

---
## 6. Tối ưu *option* mới nhất cho 3D‑Force‑Graph
1. **Physics engine** – dùng `d3-force-3d` kết hợp `ForceGraph3D` để có lực vật lý chính xác, hỗ trợ `forceManyBody` và `forceCollide`.
2. **Link curvature** – `linkCurvature={0.2}` để làm đường cong, giúp tránh chồng lên node.
3. **Particle animation** – `linkDirectionalParticles={4}` + `linkDirectionalParticleSpeed={0.006}` tạo hiệu ứng luồng dữ liệu.
4. **Node label 3D** – tạo `THREE.Mesh` với `THREE.TextGeometry` để hiển thị tên node theo chiều sâu.
5. **Performance** – bật `enableNodeHoverAnimation={false}` khi số node > 200; sử dụng `worker` để tính lực (có thể dùng `d3-force-3d` trong WebWorker).
6. **Theme** – dùng màu gradient dựa vào `type`; có chế độ **dark mode** tự động dựa vào CSS var `--color-bg`.

---
## 7. Định nghĩa *tác vụ* dễ dàng cho người dùng
1. **UI Builder** – trong giao diện 3D, cho phép kéo‑đặt node → mở modal **Create Task**.
2. **Form JSON** – modal hiển thị mẫu JSON (như mục 5.2) và tự động validate với **AJV**.
3. **Save → Deploy** – khi người dùng lưu, front‑end gọi API `/api/neutron/task` → backend lưu và **publish** MQTT.
4. **Versioning** – mỗi task có `version` để người dùng có thể rollback.

---
## 8. Tích hợp ESP32‑S3 + Lua script (biên)
### 8.1 Môi trường Lua trên ESP32‑S3
- Sử dụng **NodeMCU‑Lua** hoặc **elua** (được biên dịch cho ESP‑IDF).
- Lua script sẽ **subscribe** tới topic `espclaw/{device_id}/task` và thực thi lệnh.

### 8.2 Ví dụ Lua script nhận và thực hiện task
```lua
-- init_mqtt.lua (run at boot)
local mqtt = require('mqtt')
local client = mqtt.Client("esp32s3", 120, "myesp123", "by4@eQpAmSKTTCh")

client:on('connect', function()
  print('MQTT connected')
  client:subscribe('espclaw/'..DEVICE_ID..'/task', 1)
end)

client:on('message', function(topic, data)
  if data then
    local task = sjson.decode(data)
    print('Received task:', task.action)
    if task.action == 'read_data' then
      -- ví dụ: đọc cảm biến và trả về
      local value = adc.read(0) -- fake example
      local payload = sjson.encode({temp=value, ts=rtctime.get())
      client:publish('espclaw/'..DEVICE_ID..'/response', payload, 1)
    elseif task.action == 'set_pin' then
      gpio.write(task.payload.pin, task.payload.value)
    end
  end
end)

client:connect('mqtts://216f9e2d8c15496ab889d41cfff20880.s1.eu.hivemq.cloud', 8883)
```
### 8.3 Kết nối với Node.js backend
- Backend **publish** task dưới dạng JSON (xem mục 5.2).
- ESP32‑S3 **ack** bằng phản hồi lên topic `espclaw/{device_id}/response`.
- Front‑end có thể lắng nghe response qua WebSocket → cập nhật đồ thị (ví dụ đổi màu node thành **green** khi thành công).

---
## 9. Quy trình triển khai toàn bộ
1. **Cài đặt front‑end** → chạy `npm run dev` → xác nhận đồ thị hiển thị.
2. **Thêm cấu hình MQTT** trong `.env.local` (broker URL, user, password).
3. **Triển khai Lua script** lên ESP32‑S3 (ESP‑IDF + `make flash`).
4. **Kiểm tra**: gửi task từ UI → backend → MQTT → ESP32 → trả lời.
5. **Xem kết quả** trên đồ thị (node màu xanh -> thành công).

---
## 10. Tài liệu tham khảo & Link hữu ích
- **react-force-graph-3d**: https://github.com/vasturiano/react-force-graph
- **@react-three/fiber**: https://github.com/pmndrs/react-three-fiber
- **d3-force-3d**: https://github.com/vasturiano/d3-force-3d
- **Lua on ESP32**: https://nodemcu.readthedocs.io/en/master/en/modules/mqtt/
- **HiveMQ TLS**: https://www.hivemq.com/blog/mqtt-tls-ssl-connection/

---
### Kết luận
Với kiến trúc trên, người dùng có thể **kéo‑thả** node trên giao diện 3D, **định nghĩa** tác vụ bằng JSON, và **triển khai** nhanh trên ESP32‑S3 bằng Lua script. Các tùy chọn hiện đại (particle animation, curvature, WebWorker) giúp đồ thị luôn mượt mà ngay cả khi số lượng node tăng.

> **Bước tiếp theo**: Xác nhận các lựa chọn (công nghệ backend, lưu trữ DB, cách gửi task) để chúng tôi triển khai code thực tế.
