'use client';

import { useState } from 'react';

export default function TestPage() {
  const [result, setResult] = useState<string>('');
  const [loading, setLoading] = useState(false);

  const testHealth = async () => {
    setLoading(true);
    setResult('');
    try {
      const r = await fetch('/api/health');
      const d = await r.json();
      setResult('✅ /api/health OK\n' + JSON.stringify(d, null, 2));
    } catch (e: any) {
      setResult('❌ /api/health FAILED\n' + e.message);
    } finally {
      setLoading(false);
    }
  };

  const testOtp = async () => {
    setLoading(true);
    setResult('');
    try {
      const r = await fetch('/api/device/request-otp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ device_id: 'GETAI-1234' })
      });
      const d = await r.json();
      if (r.ok) {
        setResult('✅ /api/device/request-otp OK\n' + JSON.stringify(d, null, 2));
      } else {
        setResult('⚠️ /api/device/request-otp HTTP ' + r.status + '\n' + JSON.stringify(d, null, 2));
      }
    } catch (e: any) {
      setResult('❌ /api/device/request-otp FAILED\n' + e.message + '\n' + (e.cause ? String(e.cause) : ''));
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="min-h-screen bg-gray-950 text-white p-8 font-mono">
      <h1 className="text-3xl font-bold mb-6">🧪 ESPClaw API Test</h1>

      <div className="space-x-4 mb-6">
        <button
          onClick={testHealth}
          disabled={loading}
          className="px-4 py-2 bg-blue-600 hover:bg-blue-500 disabled:opacity-50 rounded"
        >
          Test /api/health
        </button>
        <button
          onClick={testOtp}
          disabled={loading}
          className="px-4 py-2 bg-green-600 hover:bg-green-500 disabled:opacity-50 rounded"
        >
          Test /api/device/request-otp
        </button>
      </div>

      <pre className="mt-6 p-4 bg-gray-900 rounded text-sm overflow-auto max-w-2xl whitespace-pre-wrap break-all">
        {result || 'Click a button to test'}
      </pre>

      <div className="mt-8 text-gray-400 text-sm">
        <p>📍 App: http://localhost:3000</p>
        <p>🔑 Login: http://localhost:3000</p>
        <p>🧪 Test: http://localhost:3000/test</p>
      </div>
    </div>
  );
}
