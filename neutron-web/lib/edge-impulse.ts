/**
 * neutron-web/lib/edge-impulse.ts
 *
 * Edge Impulse REST API client cho wake word training pipeline.
 *
 * Flow:
 *   1. uploadSamples()    — upload WAV files len ingestion API
 *   2. triggerTransform() — generate DSP features
 *   3. triggerTrain()     — train Keras transfer learning
 *   4. buildDeployment()  — build quantized .tflite deployment
 *   5. downloadModel()    — tai ZIP, extract .tflite bytes
 *
 * Auth: x-api-key: ei_<key>  (per-project, tu Edge Impulse Studio > Keys)
 * Upload base:  https://ingestion.edgeimpulse.com
 * Studio base: https://studio.edgeimpulse.com/v1
 */

const INGESTION_BASE = 'https://ingestion.edgeimpulse.com';
const STUDIO_BASE = 'https://studio.edgeimpulse.com/v1';

// ── Types ────────────────────────────────────────────────────────────────────

export interface EdgeImpulseProject {
  id: number;
  name: string;
  description?: string;
  category: string;
}

export interface TrainingSample {
  /** Duong dan file WAV tam thoi (ben duoi backend, /tmp/) */
  filePath: string;
  /** Nhan: "wake_word" hoac "noise" */
  label: string;
  category: 'training' | 'testing';
  /** Metadata tuy chon */
  metadata?: Record<string, string>;
}

export interface ProcessingBlock {
  id: number;
  type: string;
  name: string;
}

export interface LearnBlock {
  id: number;
  type: string;
  name: string;
}

export interface Impulse {
  inputBlocks: object[];
  dspBlocks: object[];
  learnBlocks: object[];
}

export interface JobStatus {
  jobId: number;
  type: 'transform' | 'train' | 'build';
  finished: boolean;
  finishedSuccessful: boolean;
  error?: string;
  computeTime?: number;
}

export interface DeploymentResult {
  success: boolean;
  downloadUrl: string;
  buildJobId?: number;
}

export interface TrainingResult {
  success: boolean;
  posCount: number;
  negCount: number;
  modelUrl?: string;
  tfliteBytes?: number;
  message: string;
}

// ── Core fetch wrapper ───────────────────────────────────────────────────────

async function eiFetch<T>(
  url: string,
  options: RequestInit & { apiKey: string }
): Promise<T> {
  const { apiKey, ...fetchOpts } = options;
  const res = await fetch(url, {
    ...fetchOpts,
    headers: {
      'x-api-key': apiKey,
      ...(fetchOpts.headers as Record<string, string>),
    },
  });

  if (!res.ok) {
    const text = await res.text().catch(() => res.statusText);
    throw new Error(`EI API ${res.status} on ${url}: ${text}`);
  }

  if (res.headers.get('content-type')?.includes('application/json')) {
    return res.json() as Promise<T>;
  }

  if (res.headers.get('content-type')?.includes('application/zip') ||
      res.headers.get('content-type')?.includes('binary')) {
    const buf = await res.arrayBuffer();
    return Buffer.from(buf) as unknown as T;
  }

  return res.text() as unknown as T;
}

// ── Project ──────────────────────────────────────────────────────────────────

export async function getProject(apiKey: string): Promise<EdgeImpulseProject> {
  const data = await eiFetch<{ success: boolean; project?: EdgeImpulseProject }>(
    `${STUDIO_BASE}/api/projects`,
    { apiKey }
  );
  if (!data.success || !data.project) {
    throw new Error('Khong lay duoc project Edge Impulse. Kiem tra API key.');
  }
  return data.project;
}

export async function getProjectId(apiKey: string): Promise<number> {
  const project = await getProject(apiKey);
  return project.id;
}

// ── DSP / Learning Blocks ───────────────────────────────────────────────────

export async function getProcessingBlocks(
  apiKey: string,
  projectId: number
): Promise<ProcessingBlock[]> {
  const data = await eiFetch<{ processingBlocks: ProcessingBlock[] }>(
    `${STUDIO_BASE}/api/${projectId}/processing-blocks`,
    { apiKey }
  );
  return data.processingBlocks ?? [];
}

export async function getLearnBlocks(
  apiKey: string,
  projectId: number
): Promise<LearnBlock[]> {
  const data = await eiFetch<{ learnBlocks: LearnBlock[] }>(
    `${STUDIO_BASE}/api/${projectId}/learn-blocks`,
    { apiKey }
  );
  return data.learnBlocks ?? [];
}

// ── Impulse Setup ───────────────────────────────────────────────────────────

export async function saveImpulse(
  apiKey: string,
  projectId: number,
  impulse: Impulse
): Promise<void> {
  await eiFetch(`${STUDIO_BASE}/api/${projectId}/impulse`, {
    apiKey,
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(impulse),
  });
}

// ── Sample Upload ───────────────────────────────────────────────────────────

export async function uploadSample(
  apiKey: string,
  sample: TrainingSample
): Promise<void> {
  const FormData = await import('form-data').then(m => m.default);
  const fs = await import('fs');
  const path = await import('path');

  const form = new FormData();
  const fileBuffer = fs.readFileSync(sample.filePath);
  const fileName = path.basename(sample.filePath);
  form.append('data', fileBuffer, {
    filename: fileName,
    contentType: 'audio/wav',
  });

  const headers: Record<string, string> = {
    'x-api-key': apiKey,
    'x-label': sample.label,
    'x-category': sample.category,
    ...form.getHeaders(),
  };

  if (sample.metadata) {
    headers['x-metadata'] = JSON.stringify(sample.metadata);
  }

  const res = await fetch(`${INGESTION_BASE}/api/training/files`, {
    method: 'POST',
    headers,
    body: form.getBuffer(),
  });

  if (!res.ok) {
    const text = await res.text().catch(() => String(res.status));
    throw new Error(`Upload that bai ${res.status}: ${text}`);
  }
}

export async function uploadSamples(
  apiKey: string,
  samples: TrainingSample[]
): Promise<{ uploaded: number; failed: number }> {
  let uploaded = 0;
  let failed = 0;
  for (const s of samples) {
    try {
      await uploadSample(apiKey, s);
      uploaded++;
      console.log(`[EI] Uploaded: ${s.label} — ${s.filePath}`);
    } catch (e) {
      failed++;
      console.error(`[EI] Upload failed for ${s.filePath}:`, e);
    }
  }
  return { uploaded, failed };
}

// ── Jobs ────────────────────────────────────────────────────────────────────

async function _triggerJob(
  apiKey: string,
  projectId: number,
  urlSuffix: string,
  body: object
): Promise<number> {
  const data = await eiFetch<{ success: boolean; jobId?: number; id?: number }>(
    `${STUDIO_BASE}/api/${projectId}${urlSuffix}`,
    {
      apiKey,
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    }
  );
  const jobId = data.jobId ?? data.id;
  if (!data.success || jobId == null) {
    throw new Error('Trigger job that bai');
  }
  return jobId as number;
}

async function _pollJob(
  apiKey: string,
  projectId: number,
  jobId: number,
  type: JobStatus['type'],
  maxWaitMs = 600_000,
  intervalMs = 5_000
): Promise<JobStatus> {
  const deadline = Date.now() + maxWaitMs;

  while (Date.now() < deadline) {
    const data = await eiFetch<{ success: boolean; job: object }>(
      `${STUDIO_BASE}/api/${projectId}/jobs/${jobId}/status`,
      { apiKey }
    );

    const job = data.job as Record<string, unknown>;
    const finished = Boolean(job['finished']);
    const finishedSuccessful = Boolean(job['finishedSuccessful']);

    if (finished) {
      return {
        jobId,
        type,
        finished: true,
        finishedSuccessful,
        error: finishedSuccessful ? undefined : (job['error'] as string),
        computeTime: job['computeTime'] as number | undefined,
      };
    }

    await new Promise(r => setTimeout(r, intervalMs));
  }

  return {
    jobId,
    type,
    finished: false,
    finishedSuccessful: false,
    error: 'Timeout choi qua 10 phut',
  };
}

export async function triggerTransform(
  apiKey: string,
  projectId: number,
  dspBlockId: number
): Promise<number> {
  return _triggerJob(
    apiKey, projectId,
    `/jobs/transform`,
    { dspId: dspBlockId, calculateFeatureImportance: false, skipFeatureExplorer: true }
  );
}

export async function triggerTrain(
  apiKey: string,
  projectId: number,
  learnBlockId: number,
  cycles = 100
): Promise<number> {
  return _triggerJob(
    apiKey, projectId,
    `/jobs/train-keras/${learnBlockId}`,
    {
      mode: 'visual',
      trainingCycles: cycles,
      learningRate: 0.001,
      Autoencoder: false,
      validationSetSize: '20',
    }
  );
}

export async function triggerBuild(
  apiKey: string,
  projectId: number
): Promise<number> {
  return _triggerJob(
    apiKey, projectId,
    '/jobs/build-ondevice-model',
    { engine: 'tflite-eon', modelType: 'int8' }
  );
}

export async function pollJobStatus(
  apiKey: string,
  projectId: number,
  jobId: number,
  type: JobStatus['type']
): Promise<JobStatus> {
  return _pollJob(apiKey, projectId, jobId, type);
}

// ── Full orchestration ────────────────────────────────────────────────────────

export interface TrainOptions {
  apiKey: string;
  projectId: number;
  samples: TrainingSample[];
  cycles?: number;
  dspBlockId?: number;
  learnBlockId?: number;
  onStage?: (stage: string) => void;
}

export async function runFullTrainingPipeline(
  options: TrainOptions
): Promise<TrainingResult> {
  const {
    apiKey, projectId, samples, cycles = 100,
    dspBlockId: dspId, learnBlockId: learnId,
    onStage,
  } = options;

  onStage?.('upload');

  // 1. Upload samples
  const { uploaded, failed } = await uploadSamples(apiKey, samples);
  if (uploaded === 0) {
    throw new Error('Khong upload duoc mau nao');
  }

  onStage?.('transform');

  // 2. Get DSP block if not provided
  let dspBlockId = dspId;
  if (!dspBlockId) {
    const blocks = await getProcessingBlocks(apiKey, projectId);
    const spectral = blocks.find(b =>
      b.type === 'spectral-analysis' || b.type === 'mfcc' || b.type === 'mfe'
    );
    if (!spectral) throw new Error('Khong tim thay DSP block phu hop');
    dspBlockId = spectral.id;
  }

  // 3. Trigger transform
  const transformJobId = await triggerTransform(apiKey, projectId, dspBlockId);
  const transformResult = await _pollJob(apiKey, projectId, transformJobId, 'transform');
  if (!transformResult.finishedSuccessful) {
    throw new Error(`Transform that bai: ${transformResult.error}`);
  }

  onStage?.('train');

  // 4. Get learn block if not provided
  let learnBlockId = learnId;
  if (!learnBlockId) {
    const blocks = await getLearnBlocks(apiKey, projectId);
    const keras = blocks.find(b => b.type === 'keras' || b.type === 'transfer-learning');
    if (!keras) throw new Error('Khong tim thay Keras learning block');
    learnBlockId = keras.id;
  }

  // 5. Trigger train
  const trainJobId = await triggerTrain(apiKey, projectId, learnBlockId, cycles);
  const trainResult = await _pollJob(apiKey, projectId, trainJobId, 'train');
  if (!trainResult.finishedSuccessful) {
    throw new Error(`Train that bai: ${trainResult.error}`);
  }

  onStage?.('build');

  // 6. Build deployment
  const buildJobId = await triggerBuild(apiKey, projectId);
  const buildResult = await _pollJob(apiKey, projectId, buildJobId, 'build');
  if (!buildResult.finishedSuccessful) {
    throw new Error(`Build that bai: ${buildResult.error}`);
  }

  onStage?.('done');

  const posCount = samples.filter(s => s.type === 'pos').length;
  const negCount = samples.filter(s => s.type === 'neg').length;

  return {
    success: true,
    posCount: samples.filter(s => s.label !== 'noise').length,
    negCount: samples.filter(s => s.label === 'noise').length,
    message: `Train xong! pos=${uploaded - failed}, neg=0, cycles=${cycles}`,
  };
}

// ── Model download ──────────────────────────────────────────────────────────

export async function downloadDeploymentZip(
  apiKey: string,
  projectId: number
): Promise<Buffer> {
  const buf = await eiFetch<Buffer>(
    `${STUDIO_BASE}/api/${projectId}/deployment/download?type=zip&engine=tflite-eon&modelType=int8`,
    {
      apiKey,
      headers: { 'Accept': 'application/zip' },
    }
  );
  return buf as unknown as Buffer;
}

/**
 * Extract the .tflite file bytes from an Edge Impulse deployment ZIP.
 * The model lives at: tflite-model/trained_model.tflite
 * or: model-parameters/training_model.tflite
 */
export async function extractTfliteFromZip(zipBuffer: Buffer): Promise<Buffer> {
  const zip = await import('jszip').then(m => m.default);
  const archive = await zip.loadAsync(zipBuffer);

  // Try multiple known paths for the .tflite model
  const knownPaths = [
    'tflite-model/trained_model.tflite',
    'tflite-model/trained_model_compiled.cpp',
    'model-parameters/training_model.tflite',
    'model.eim',
  ];

  for (const path of knownPaths) {
    const file = archive.file(path);
    if (file) {
      const content = await file.async('nodebuffer');
      console.log(`[EI] Model found at: ${path} (${content.length} bytes)`);
      return content;
    }
  }

  // List all files for debugging
  const allFiles = Object.keys(archive.files);
  console.error('[EI] ZIP contents:', allFiles.join(', '));
  throw new Error('Khong tim thay file .tflite trong ZIP. Thu tai Edge Impulse console.');
}
