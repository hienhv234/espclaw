/**
 * Backend xử lý wake word theo 3 mode → MQTT sync ESP
 */
import { getMQTTPool } from '@/lib/mqtt-pool';
import {
  WakewordDeviceConfig,
  WakewordSourceMode,
  buildWakewordSyncPayload,
  getLibraryPreset,
  suggestThresholdForKeyword,
} from '@/lib/wakeword';
import type { WakewordTrainSample } from '@/lib/validation';
import { getProject, runFullTrainingPipeline, type TrainingSample } from '@/lib/edge-impulse';
import * as fs from 'fs';
import * as path from 'path';

export interface ProcessResult {
  config: WakewordDeviceConfig;
  message: string;
  synced: boolean;
}

async function mqttSync(deviceId: string, config: WakewordDeviceConfig): Promise<void> {
  const pool = getMQTTPool();
  const topic = `espclaw/${deviceId}/cmd`;
  const payload = JSON.stringify(buildWakewordSyncPayload(config));
  await pool.publish(topic, payload, 1);
}

/** Mode 1: Thư viện có sẵn */
export async function processLibrary(
  deviceId: string,
  presetId: string,
  enabled: boolean
): Promise<ProcessResult> {
  const preset = getLibraryPreset(presetId);
  if (!preset) {
    throw new Error(`Preset không có trong thư viện: ${presetId}`);
  }

  const config: WakewordDeviceConfig = {
    source_mode: 'library',
    enabled,
    threshold: preset.threshold,
    preset: preset.id,
    label: preset.label,
    model_version: 1,
  };

  await mqttSync(deviceId, config);

  return {
    config,
    message: `Đã áp dụng thư viện "${preset.label}" (${preset.modelHint ?? 'stub RMS'}) và đồng bộ ESP.`,
    synced: true,
  };
}

/** Mode 2: Nhập từ khóa — backend chuẩn hóa & tính ngưỡng */
export async function processKeyword(
  deviceId: string,
  keyword: string,
  enabled: boolean
): Promise<ProcessResult> {
  const normalized = keyword.trim().replace(/\s+/g, ' ');
  if (normalized.length < 2) {
    throw new Error('Từ khóa phải có ít nhất 2 ký tự');
  }
  if (normalized.length > 32) {
    throw new Error('Từ khóa tối đa 32 ký tự');
  }

  const threshold = suggestThresholdForKeyword(normalized);

  const config: WakewordDeviceConfig = {
    source_mode: 'keyword',
    enabled,
    threshold,
    keyword: normalized,
    label: normalized,
    model_version: 1,
  };

  await mqttSync(deviceId, config);

  return {
    config,
    message: `Từ khóa "${normalized}" đã lưu (ngưỡng ${threshold}). Đồng bộ ESP — engine stub dùng RMS; Phase 2 sẽ gắn KWS theo từ.`,
    synced: true,
  };
}

/** Stub train: phân tích số mẫu → ngưỡng + model_version */
function analyzeSamplesForTrain(
  samples: WakewordTrainSample[],
  label: string
): { threshold: number; modelVersion: number; pos: number; neg: number } {
  const pos = samples.filter((s) => s.type === 'pos').length;
  const neg = samples.filter((s) => s.type === 'neg').length;
  const ratio = pos / Math.max(neg, 1);
  let threshold = 1200;
  if (ratio < 0.5) threshold = 1350;
  else if (ratio > 1.2) threshold = 1050;
  const modelVersion =
    (Math.abs(label.split('').reduce((a, c) => a + c.charCodeAt(0), 0)) % 900) + 100;
  return { threshold, modelVersion, pos, neg };
}

/** Mode 3: Thu mẫu dương/âm → backend xử lý → sync ESP */
export async function processTrain(
  deviceId: string,
  label: string,
  samples: WakewordTrainSample[],
  enabled: boolean
): Promise<ProcessResult> {
  const apiKey = process.env.EDGE_IMPULSE_API_KEY;
  if (!apiKey) {
    throw new Error('Chưa cấu hình EDGE_IMPULSE_API_KEY trong .env.local. Hãy kiểm tra lại.');
  }

  // 1. Kiểm tra kết nối với dự án Edge Impulse & Lấy ID dự án
  console.log('[train] Đang kiểm tra dự án Edge Impulse...');
  const project = await getProject(apiKey);
  const projectId = project.id;
  console.log(`[train] Kết nối thành công! Project: ${project.name} (ID: ${projectId})`);

  // 2. Chuyển đổi các mẫu sang định dạng Edge Impulse TrainingSample
  const eiSamples: TrainingSample[] = [];
  const sharedAudioDir = '/tmp/espclaw-audio';

  for (const s of samples) {
    if (s.source === 'esp' && s.fileName) {
      const filePath = path.join(sharedAudioDir, s.fileName);
      if (fs.existsSync(filePath)) {
        eiSamples.push({
          filePath,
          label: s.type === 'pos' ? label.trim().toLowerCase().replace(/\s+/g, '_') : 'noise',
          category: 'training',
        });
      } else {
        console.warn(`[train] Không tìm thấy file mẫu trên đĩa: ${filePath}`);
      }
    }
  }

  if (eiSamples.length === 0) {
    throw new Error('Không tìm thấy tệp tin âm thanh mẫu nào hợp lệ trong /tmp/espclaw-audio.');
  }

  const pos = eiSamples.filter(s => s.label !== 'noise').length;
  const neg = eiSamples.filter(s => s.label === 'noise').length;

  console.log(`[train] Bắt đầu kích hoạt Pipeline huấn luyện trên Edge Impulse Cloud (Pos: ${pos}, Neg: ${neg})...`);

  // 3. Chạy Pipeline huấn luyện đầy đủ trên Edge Impulse Cloud
  // (Lượng tử hóa int8, trích xuất MFCC/MFE, train Keras và build model)
  const trainResult = await runFullTrainingPipeline({
    apiKey,
    projectId,
    samples: eiSamples,
    cycles: 30, // Thiết lập số chu kỳ vừa phải để tối ưu thời gian huấn luyện
  });

  if (!trainResult.success) {
    throw new Error('Huấn luyện mô hình thất bại trên Edge Impulse.');
  }

  console.log('[train] Edge Impulse pipeline hoàn thành mỹ mãn!');

  // 4. Tính toán cấu hình để đồng bộ xuống ESP
  const { threshold, modelVersion } = analyzeSamplesForTrain(samples, label);
  const jobId = `train-${deviceId}-${Date.now()}`;

  const config: WakewordDeviceConfig = {
    source_mode: 'train',
    enabled,
    threshold,
    label: label.trim(),
    model_version: modelVersion,
    train_job_id: jobId,
  };

  const pool = getMQTTPool();
  const topic = `espclaw/${deviceId}/cmd`;

  // Gửi tín hiệu trạng thái qua MQTT báo cho ESP biết
  await pool.publish(
    topic,
    JSON.stringify({
      type: 'wakeword',
      action: 'train_start',
      source_mode: 'train',
      label: config.label,
      ts: Date.now(),
    }),
    1
  );

  for (const s of samples) {
    await pool.publish(
      topic,
      JSON.stringify({
        type: 'wakeword',
        action: 'train_record',
        source_mode: 'train',
        sample_type: s.type,
        label: config.label,
        ts: Date.now(),
      }),
      1
    );
  }

  await pool.publish(
    topic,
    JSON.stringify({
      type: 'wakeword',
      action: 'train_finish',
      source_mode: 'train',
      label: config.label,
      model_version: modelVersion,
      threshold,
      ts: Date.now(),
    }),
    1
  );

  // Đồng bộ cấu hình xuống NVS của ESP
  await mqttSync(deviceId, config);

  return {
    config,
    message: `Edge Impulse Train thành công! Đã nạp ${pos} mẫu dương, ${neg} mẫu âm vào dự án Cloud. Cập nhật ESP thành công (ngưỡng ${threshold}, model v${modelVersion}).`,
    synced: true,
  };
}

export async function processWakewordApply(body: {
  mode: WakewordSourceMode;
  deviceId: string;
  enabled: boolean;
  presetId?: string;
  keyword?: string;
  label?: string;
  samples?: WakewordTrainSample[];
}): Promise<ProcessResult> {
  switch (body.mode) {
    case 'library':
      if (!body.presetId) throw new Error('presetId bắt buộc cho mode library');
      return processLibrary(body.deviceId, body.presetId, body.enabled);
    case 'keyword':
      if (!body.keyword) throw new Error('keyword bắt buộc cho mode keyword');
      return processKeyword(body.deviceId, body.keyword, body.enabled);
    case 'train':
      if (!body.samples?.length) throw new Error('samples bắt buộc cho mode train');
      return processTrain(
        body.deviceId,
        body.label ?? 'custom',
        body.samples,
        body.enabled
      );
    default:
      throw new Error('mode không hợp lệ');
  }
}
