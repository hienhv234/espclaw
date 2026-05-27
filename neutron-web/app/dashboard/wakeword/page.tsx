'use client';

import { useEffect, useRef, useState } from 'react';
import Link from 'next/link';
import { WAKEWORD_LIBRARY, type WakewordSourceMode, type WakewordDeviceConfig } from '@/lib/wakeword';

type TabId = WakewordSourceMode;

interface DeviceInfo {
  device_id: string;
  tenant_id?: string;
}

interface MicTestResult {
  url: string;
  fileName: string;
  durationMs: number;
}

interface TrainSample {
  id: string;
  type: 'pos' | 'neg';
  fileName: string;
  url: string;
}

const TABS: { id: TabId; title: string; moTa: string }[] = [
  {
    id: 'library',
    title: '1. Thu Vien Co San',
    moTa: 'Chon wake word tu thu vien firmware — khong can ghi am',
  },
  {
    id: 'keyword',
    title: '2. Nhap Tu Khoa',
    moTa: 'Nhap cum tu -> backend xu ly & tinh nguong -> dong bo ESP',
  },
  {
    id: 'train',
    title: '3. Thu Mau Train',
    moTa: 'Ghi am tu mic ESP -> backend xu ly -> dong bo ESP',
  },
];

export default function WakewordPage() {
  const [device, setDevice] = useState<DeviceInfo | null>(null);
  const [tab, setTab] = useState<TabId>('library');
  const [enabled, setEnabled] = useState(true);
  const [libraryPresetId, setLibraryPresetId] = useState('hi_esp');
  const [keyword, setKeyword] = useState('');
  const [trainLabel, setTrainLabel] = useState('E Claw');

  /* Train samples collected from ESP mic */
  const [trainSamples, setTrainSamples] = useState<TrainSample[]>([]);
  const [recording, setRecording] = useState<'pos' | 'neg' | null>(null);
  const [micTestAudio, setMicTestAudio] = useState<MicTestResult | null>(null);
  const [micTestLoading, setMicTestLoading] = useState(false);

  const [status, setStatus] = useState('');
  const [busy, setBusy] = useState(false);
  const [lastConfig, setLastConfig] = useState<WakewordDeviceConfig | null>(null);
  const audioRef = useRef<HTMLAudioElement>(null);

  useEffect(() => {
    const stored = localStorage.getItem('espclaw_device');
    if (!stored) { window.location.href = '/'; return; }
    setDevice(JSON.parse(stored) as DeviceInfo);
  }, []);

  /* --- API helpers --- */

  const callApi = async (payload: Record<string, unknown>) => {
    if (!device) return;
    setBusy(true);
    setStatus('Dang xu ly...');
    try {
      const res = await fetch('/api/wakeword/apply', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ deviceId: device.device_id, enabled, ...payload }),
      });
      const data = await res.json();
      if (res.ok) {
        setLastConfig(data.config ?? null);
        setStatus(`✓ ${data.message}`);
      } else {
        setStatus(`✗ ${data.error}${data.details ? ` — ${JSON.stringify(data.details)}` : ''}`);
      }
    } catch {
      setStatus('✗ Loi ket noi API');
    } finally {
      setBusy(false);
    }
  };

  /* Record from ESP mic */
  const recordFromEsp = async (type: 'pos' | 'neg'): Promise<{ url: string; fileName: string } | null> => {
    if (!device) return null;
    setRecording(type);
    setStatus(`Dang thu am tu ESP (${type === 'pos' ? 'duong' : 'am'})...`);
    try {
      const res = await fetch('/api/wakeword/mic-test', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ deviceId: device.device_id, durationMs: 1800 }),
      });

      if (!res.ok) {
        const text = await res.text().catch(() => '');
        throw new Error(text || `Loi ${res.status}`);
      }

      // POST tra ve WAV blob truc tiep
      const blob = await res.blob();
      const url = URL.createObjectURL(blob);
      const fileName = res.headers.get('X-File-Name') || `${type}-${Date.now()}.wav`;
      setStatus(`${type === 'pos' ? 'Mau duong' : 'Mau am'} tu ESP da san.`);
      return { url, fileName };
    } catch (e: unknown) {
      setStatus(`✗ Thu am ESP that bai: ${e instanceof Error ? e.message : String(e)}`);
      return null;
    } finally {
      setRecording(null);
    }
  };

  /* Test mic button */
  const handleMicTest = async () => {
    if (!device) return;
    setMicTestLoading(true);
    setStatus('Dang thu am tu ESP...');
    setMicTestAudio(null);
    try {
      const res = await fetch('/api/wakeword/mic-test', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ deviceId: device.device_id, durationMs: 1800 }),
      });

      if (!res.ok) {
        const text = await res.text().catch(() => '');
        throw new Error(text || `Loi ${res.status}`);
      }

      // POST tra ve WAV blob truc tiep
      const blob = await res.blob();
      const url = URL.createObjectURL(blob);
      const chunkCount = parseInt(res.headers.get('X-Chunk-Count') ?? '0', 10);
      const durationMs = parseInt(res.headers.get('X-Duration-Ms') ?? '1800', 10);
      setMicTestAudio({ url, fileName: 'mic_test.wav', durationMs });
      setStatus(`Thu am thanh cong! ${chunkCount} chunks`);
    } catch (e: unknown) {
      setStatus(`✗ Test mic that bai: ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      setMicTestLoading(false);
    }
  };

  /* Add sample from ESP recording */
  const handleAddSample = async (type: 'pos' | 'neg') => {
    const sample = await recordFromEsp(type);
    if (!sample) return;
    setTrainSamples(prev => [
      ...prev,
      { id: `${type}-${Date.now()}`, type, fileName: sample.fileName, url: sample.url },
    ]);
  };

  const handleLibraryApply = () => callApi({ mode: 'library', presetId: libraryPresetId });

  const handleKeywordApply = () => {
    if (!keyword.trim()) { setStatus('Nhap tu khoa wake word.'); return; }
    callApi({ mode: 'keyword', keyword: keyword.trim() });
  };

  const handleTrainApply = async () => {
    if (!device) return;
    const pos = trainSamples.filter(s => s.type === 'pos').length;
    const neg = trainSamples.filter(s => s.type === 'neg').length;
    if (pos < 3 || neg < 3) {
      setStatus(`Can it nhat 3 mau duong va 3 mau am. Hien co: ${pos} duong, ${neg} am.`);
      return;
    }
    setBusy(true);
    setStatus('Dang upload mau & train...');
    try {
      const res = await fetch('/api/wakeword/apply', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          mode: 'train',
          deviceId: device.device_id,
          enabled,
          label: trainLabel.trim() || 'custom',
          samples: trainSamples.map(s => ({
            type: s.type,
            audioBase64: '', // backend does not need base64 for ESP-sourced samples
            source: 'esp',
            fileName: s.fileName,
          })),
        }),
      });
      const data = await res.json();
      if (res.ok) {
        setLastConfig(data.config ?? null);
        setStatus(`✓ ${data.message}`);
      } else {
        setStatus(`✗ ${data.error}`);
      }
    } catch {
      setStatus('✗ Train that bai');
    } finally {
      setBusy(false);
    }
  };

  const posCount = trainSamples.filter(s => s.type === 'pos').length;
  const negCount = trainSamples.filter(s => s.type === 'neg').length;

  if (!device) {
    return (
      <div className="min-h-screen bg-gray-950 flex items-center justify-center text-gray-400">
        Dang tai...
      </div>
    );
  }

  return (
    <div className="min-h-screen bg-gray-950 text-white">
      {/* Header */}
      <header className="border-b border-white/5 bg-gray-900/80 backdrop-blur sticky top-0 z-20">
        <div className="max-w-4xl mx-auto px-6 py-3 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <span className="text-2xl">🎤</span>
            <div>
              <h1 className="text-lg font-bold">Wake Word</h1>
              <p className="text-xs text-gray-400 font-mono">{device.device_id}</p>
            </div>
          </div>
          <Link href="/dashboard" className="px-3 py-2 text-sm bg-gray-800 hover:bg-gray-700 rounded-lg text-gray-300">
            ← Workflow
          </Link>
        </div>
      </header>

      <main className="max-w-4xl mx-auto px-6 py-6 space-y-6">

        {/* ON/OFF */}
        <div className="flex flex-wrap items-center gap-4 p-4 rounded-xl bg-gray-900/60 border border-white/10">
          <span className="text-sm text-gray-400">Wake word tren ESP</span>
          <button
            type="button"
            onClick={() => setEnabled(!enabled)}
            className={`relative w-14 h-8 rounded-full transition ${enabled ? 'bg-emerald-600' : 'bg-gray-700'}`}
          >
            <span className={`absolute top-1 left-1 w-6 h-6 bg-white rounded-full transition ${enabled ? 'translate-x-6' : ''}`} />
          </button>
          <span className="text-sm font-medium">{enabled ? 'Bat' : 'Tat'}</span>
          <span className="text-xs text-gray-500">Ap dung khi nhan nut dong bo o tung tab</span>
        </div>

        {/* 3 tab */}
        <div className="flex flex-col sm:flex-row gap-2">
          {TABS.map(t => (
            <button
              key={t.id}
              type="button"
              onClick={() => setTab(t.id)}
              className={`flex-1 text-left p-4 rounded-xl border transition ${
                tab === t.id
                  ? 'border-purple-500 bg-purple-600/15'
                  : 'border-white/10 bg-gray-900/40 hover:border-white/20'
              }`}
            >
              <div className="font-medium text-sm">{t.title}</div>
              <div className="text-xs text-gray-500 mt-1">{t.moTa}</div>
            </button>
          ))}
        </div>

        {/* Tab content */}
        <div className="bg-gray-900/60 border border-white/10 rounded-xl p-6 space-y-5">

          {/* TAB 1: Thu vien */}
          {tab === 'library' && (
            <>
              <p className="text-sm text-gray-400">
                Chon mot wake word tu thu vien. Khong can ghi am.
              </p>
              <div className="grid gap-2 sm:grid-cols-2">
                {WAKEWORD_LIBRARY.map(p => (
                  <button
                    key={p.id}
                    type="button"
                    onClick={() => setLibraryPresetId(p.id)}
                    className={`text-left p-4 rounded-lg border ${
                      libraryPresetId === p.id
                        ? 'border-emerald-500 bg-emerald-600/10'
                        : 'border-white/10 hover:bg-gray-800/50'
                    }`}
                  >
                    <div className="font-medium">{p.label}</div>
                    <div className="text-xs text-gray-500 mt-1">{p.description}</div>
                    {p.modelHint && <div className="text-xs text-gray-600 mt-1 font-mono">{p.modelHint}</div>}
                  </button>
                ))}
              </div>
              <button
                type="button"
                disabled={busy}
                onClick={handleLibraryApply}
                className="px-5 py-2.5 bg-blue-600 hover:bg-blue-500 disabled:opacity-50 rounded-lg text-sm font-medium"
              >
                📡 Ap dung thu vien & dong bo ESP
              </button>
            </>
          )}

          {/* TAB 2: Tu khoa */}
          {tab === 'keyword' && (
            <>
              <p className="text-sm text-gray-400">
                Nhap cum tu ban muon lam wake word. Backend se chuan hoa, tinh nguong RMS, roi gui
                MQTT xuong ESP.
              </p>
              <div>
                <label className="text-xs text-gray-500 block mb-1">Tu khoa</label>
                <input
                  value={keyword}
                  onChange={e => setKeyword(e.target.value)}
                  placeholder='VD: "E Claw", "Xin chao Neutron"'
                  className="w-full max-w-md bg-gray-800 border border-white/10 rounded-lg px-3 py-2.5 text-sm"
                  maxLength={32}
                />
              </div>
              <button
                type="button"
                disabled={busy || !keyword.trim()}
                onClick={handleKeywordApply}
                className="px-5 py-2.5 bg-blue-600 hover:bg-blue-500 disabled:opacity-50 rounded-lg text-sm font-medium"
              >
                📡 Xu ly tu khoa & dong bo ESP
              </button>
            </>
          )}

          {/* TAB 3: Train */}
          {tab === 'train' && (
            <>
              <p className="text-sm text-gray-400">
                Thu am <strong className="text-emerald-400">tu mic ESP</strong>. Can it nhat 3 mau{' '}
                <strong className="text-emerald-400">duong</strong> (noi dung wake word) va 3 mau{' '}
                <strong className="text-amber-400">am</strong> (cau khac / on). Nhan nut ben duoi,
                sau do nhan <strong>+ Mau Duong</strong> hoac <strong>+ Mau Am</strong>.
              </p>

              {/* Nut test mic */}
              <div className="flex flex-wrap gap-2 items-center">
                <button
                  type="button"
                  disabled={micTestLoading || !!recording}
                  onClick={handleMicTest}
                  className="px-4 py-2 bg-cyan-600/80 hover:bg-cyan-500 disabled:opacity-50 rounded-lg text-sm font-medium flex items-center gap-2"
                >
                  {micTestLoading ? '⏺ Dang thu...' : '🎙 Test Mic ESP'}
                </button>
                {micTestAudio && (
                  <>
                    <audio
                      ref={audioRef}
                      src={micTestAudio.url}
                      controls
                      className="h-9"
                    />
                    <span className="text-xs text-gray-500">
                      {micTestAudio.fileName} ({(micTestAudio.durationMs / 1000).toFixed(1)}s)
                    </span>
                  </>
                )}
              </div>

              {/* Mau duong / am tu ESP */}
              <div>
                <label className="text-xs text-gray-500 block mb-1">Ten wake word (train)</label>
                <input
                  value={trainLabel}
                  onChange={e => setTrainLabel(e.target.value)}
                  className="w-full max-w-xs bg-gray-800 border border-white/10 rounded-lg px-3 py-2 text-sm"
                  maxLength={32}
                />
              </div>

              <div className="flex flex-wrap gap-2">
                <button
                  type="button"
                  disabled={!!recording || busy}
                  onClick={() => handleAddSample('pos')}
                  className="px-4 py-2 bg-emerald-600/80 hover:bg-emerald-500 disabled:opacity-50 rounded-lg text-sm"
                >
                  {recording === 'pos' ? '⏺ Dang ghi...' : '+ Mau Duong'}
                </button>
                <button
                  type="button"
                  disabled={!!recording || busy}
                  onClick={() => handleAddSample('neg')}
                  className="px-4 py-2 bg-amber-600/80 hover:bg-amber-500 disabled:opacity-50 rounded-lg text-sm"
                >
                  {recording === 'neg' ? '⏺ Dang ghi...' : '+ Mau Am'}
                </button>
                <button
                  type="button"
                  disabled={!!recording}
                  onClick={() => setTrainSamples([])}
                  className="px-4 py-2 bg-gray-700 rounded-lg text-sm text-gray-300"
                >
                  Xoa mau
                </button>
              </div>

              <p className="text-sm text-gray-500">
                Da ghi: <span className="text-emerald-400">{posCount} duong</span> ·{' '}
                <span className="text-amber-400">{negCount} am</span>
              </p>

              {trainSamples.length > 0 && (
                <div className="space-y-1">
                  {trainSamples.map(s => (
                    <div key={s.id} className="flex items-center gap-2 text-xs text-gray-400">
                      <span className={s.type === 'pos' ? 'text-emerald-400' : 'text-amber-400'}>
                        [{s.type.toUpperCase()}]
                      </span>
                      <audio src={s.url} controls className="h-6 flex-1" />
                    </div>
                  ))}
                </div>
              )}

              <button
                type="button"
                disabled={busy || posCount < 3 || negCount < 3}
                onClick={handleTrainApply}
                className="px-5 py-2.5 bg-purple-600 hover:bg-purple-500 disabled:opacity-40 rounded-lg text-sm font-medium"
              >
                🧠 Xu ly train & dong bo ESP
              </button>
              <p className="text-xs text-gray-600">
                Phase 2: Edge Impulse build KWS model. Hien tai: backend xu ly stub + sync ESP.
              </p>
            </>
          )}
        </div>

        {/* Ket qua cau hinh cuoi */}
        {lastConfig && (
          <div className="text-xs font-mono p-4 rounded-lg bg-gray-800/80 border border-white/10 text-gray-400 space-y-1">
            <div>Che do: {lastConfig.source_mode}</div>
            <div>Label: {lastConfig.label}</div>
            <div>Nguong: {lastConfig.threshold}</div>
            {lastConfig.preset && <div>Preset: {lastConfig.preset}</div>}
            {lastConfig.keyword && <div>Tu khoa: {lastConfig.keyword}</div>}
            {lastConfig.model_version != null && <div>Model: v{lastConfig.model_version}</div>}
          </div>
        )}

        {/* Trang thai */}
        {status && (
          <div className="text-sm p-4 rounded-lg bg-gray-800/80 border border-white/10 text-gray-300">
            {status}
          </div>
        )}
      </main>
    </div>
  );
}
