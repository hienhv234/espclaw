'use client';

import { useState, useEffect } from 'react';
import { useRouter } from 'next/navigation';
import { getSupabaseClient } from '@/lib/supabase';

export default function LoginPage() {
  const [step, setStep] = useState<'device' | 'otp'>('device');
  const [deviceId, setDeviceId] = useState('');
  const [otp, setOtp] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [success, setSuccess] = useState(false);
  const router = useRouter();

  const handleRequestOtp = async (e: React.FormEvent) => {
    e.preventDefault();
    const pattern = /^GETAI-[A-Z0-9]{3,10}$/;
    if (!pattern.test(deviceId.toUpperCase())) {
      setError('Mã thiết bị phải có định dạng GETAI-XXXXX (3-10 ký tự sau dấu gạch)');
      return;
    }

    setError(null);
    setLoading(true);

    try {
      const res = await fetch('/api/device/request-otp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ device_id: deviceId.toUpperCase() })
      });

      const data = await res.json();
      if (!res.ok) throw new Error(data.error || 'Lỗi yêu cầu OTP');

      setStep('otp');
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  };

  const handleVerifyOtp = async (e: React.FormEvent) => {
    e.preventDefault();
    if (otp.length !== 6) {
      setError('Mã OTP phải có 6 chữ số');
      return;
    }

    setError(null);
    setLoading(true);

    try {
      const res = await fetch('/api/device/verify-otp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ 
          device_id: deviceId.toUpperCase(),
          otp 
        })
      });

      const data = await res.json();
      if (!res.ok) throw new Error(data.error || 'Mã OTP không chính xác');

      setSuccess(true);
      // Save session info
      localStorage.setItem('espclaw_device', JSON.stringify(data.device));

      setTimeout(() => {
        router.push('/dashboard');
      }, 1500);

    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="min-h-screen bg-gradient-to-br from-gray-900 via-purple-900 to-gray-900 flex items-center justify-center p-4">
      <div className="w-full max-w-md">
        {/* Logo */}
        <div className="text-center mb-8">
          <div className="text-6xl mb-4">🤖</div>
          <h1 className="text-4xl font-bold text-white mb-2">ESPClaw</h1>
          <p className="text-gray-400">Hệ thống điều khiển AI</p>
        </div>

        {/* Main Card */}
        <div className="bg-gray-800/80 backdrop-blur rounded-2xl shadow-2xl p-8 border border-white/10">
          {!success ? (
            <>
              <div className="text-center mb-6">
                <div className="text-4xl mb-2">{step === 'device' ? '📱' : '🔑'}</div>
                <h2 className="text-xl font-semibold text-white">
                  {step === 'device' ? 'Kết nối thiết bị' : 'Xác thực OTP'}
                </h2>
                <p className="text-gray-400 text-sm mt-1">
                  {step === 'device' 
                    ? 'Nhập Device ID hiển thị trên màn hình OLED' 
                    : 'Nhập mã 6 chữ số vừa hiện trên màn hình thiết bị'}
                </p>
              </div>

              {error && (
                <div className="bg-red-500/20 border border-red-500/50 text-red-300 px-4 py-3 rounded-lg mb-4 text-sm">
                  {error}
                </div>
              )}

              {step === 'device' ? (
                <form onSubmit={handleRequestOtp}>
                  <div className="mb-6">
                    <input
                      type="text"
                      value={deviceId}
                      onChange={(e) => setDeviceId(e.target.value.toUpperCase())}
                      className="w-full text-center text-2xl font-mono py-4 px-6 bg-gray-900 border-2 border-gray-700 rounded-xl focus:border-purple-500 focus:outline-none transition text-white"
                      placeholder="GETAI-XXXXX"
                      autoFocus
                    />
                  </div>

                  <button
                    type="submit"
                    disabled={loading || !deviceId}
                    className="w-full py-4 bg-gradient-to-r from-purple-600 to-blue-600 text-white rounded-xl font-semibold text-lg hover:opacity-90 transition disabled:opacity-50"
                  >
                    {loading ? 'Đang gửi...' : 'Tiếp tục'}
                  </button>
                </form>
              ) : (
                <form onSubmit={handleVerifyOtp}>
                  <div className="mb-6">
                    <input
                      type="text"
                      inputMode="numeric"
                      maxLength={6}
                      value={otp}
                      onChange={(e) => setOtp(e.target.value.replace(/\D/g, ''))}
                      className="w-full text-center text-4xl font-mono font-bold tracking-[0.3em] py-4 px-6 bg-gray-900 border-2 border-gray-700 rounded-xl focus:border-purple-500 focus:outline-none transition text-white"
                      placeholder="000000"
                      autoFocus
                    />
                  </div>

                  <div className="flex gap-3">
                    <button
                      type="button"
                      onClick={() => setStep('device')}
                      className="flex-1 py-4 bg-gray-700 text-white rounded-xl font-semibold hover:bg-gray-600 transition"
                    >
                      Quay lại
                    </button>
                    <button
                      type="submit"
                      disabled={loading || otp.length !== 6}
                      className="flex-[2] py-4 bg-gradient-to-r from-green-600 to-emerald-600 text-white rounded-xl font-semibold text-lg hover:opacity-90 transition disabled:opacity-50"
                    >
                      {loading ? 'Đang xác thực...' : 'Đăng nhập'}
                    </button>
                  </div>
                </form>
              )}
            </>
          ) : (
            <div className="text-center py-8">
              <div className="text-6xl mb-4 animate-bounce">✅</div>
              <h2 className="text-2xl font-bold text-white mb-2">Đăng nhập thành công!</h2>
              <p className="text-gray-400">Đang chuyển đến dashboard...</p>
            </div>
          )}
        </div>

        <div className="mt-6 text-center">
          <p className="text-gray-500 text-sm">
            {step === 'device' 
              ? 'Mã thiết bị có dạng GETAI-XXXXX' 
              : 'Kiểm tra màn hình OLED để lấy mã OTP'}
          </p>
        </div>
      </div>
    </div>
  );
}
