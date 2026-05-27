'use client';

import React, { useState, useEffect } from 'react';

interface SettingsModalProps {
  isOpen: boolean;
  onClose: () => void;
  tenantId: string;
}

interface DeviceConfig {
  telegram: { bot_token: string; chat_id: string };
  llm: { provider: string; api_key: string; base_url: string; model: string };
  gpio: { 
    sda: number; scl: number; 
    mic_sck: number; mic_ws: number; mic_sd: number;
    spk_bclk: number; spk_lrc: number; spk_dout: number;
  };
}

export default function SettingsModal({ isOpen, onClose, tenantId }: SettingsModalProps) {
  const [config, setConfig] = useState<DeviceConfig>({
    telegram: { bot_token: '', chat_id: '' },
    llm: { provider: 'anthropic', api_key: '', base_url: '', model: '' },
    gpio: { sda: 1, scl: 2, mic_sck: 41, mic_ws: 42, mic_sd: 40, spk_bclk: 4, spk_lrc: 5, spk_dout: 6 }
  });
  const [loading, setLoading] = useState(false);
  const [saving, setSaving] = useState(false);
  const [activeTab, setActiveTab] = useState<'telegram' | 'llm' | 'gpio'>('telegram');

  useEffect(() => {
    if (isOpen && tenantId) {
      loadConfigs();
    }
  }, [isOpen, tenantId]);

  const loadConfigs = async () => {
    setLoading(true);
    try {
      const ids = [`tg-${tenantId}`, `llm-${tenantId}`, `gpio-${tenantId}`];
      const results = await Promise.all(
        ids.map(id => fetch(`/api/nodes/${id}`).then(r => r.json()))
      );

      const newConfig = { ...config };
      
      if (results[0].node) {
        newConfig.telegram = {
          bot_token: results[0].node.content.bot_token || '',
          chat_id: results[0].node.content.chat_id || ''
        };
      }
      if (results[1].node) {
        newConfig.llm = {
          provider: results[1].node.content.provider || 'anthropic',
          api_key: results[1].node.content.api_key || '',
          base_url: results[1].node.content.base_url || '',
          model: results[1].node.content.model || ''
        };
      }
      if (results[2].node) {
        newConfig.gpio = { ...results[2].node.content };
      }
      
      setConfig(newConfig);
    } catch (err) {
      console.error('Failed to load configs:', err);
    } finally {
      setLoading(false);
    }
  };

  const handleSave = async () => {
    setSaving(true);
    try {
      const updates = [
        { id: `tg-${tenantId}`, data: { name: 'Telegram Interface', type: 'webhook', subtype: 'interface_telegram', content: config.telegram, tenant_id: tenantId } },
        { id: `llm-${tenantId}`, data: { name: 'LLM Configuration', type: 'skill', subtype: 'llm_config', content: config.llm, tenant_id: tenantId } },
        { id: `gpio-${tenantId}`, data: { name: 'Hardware Pins', type: 'device', subtype: 'gpio_config', content: config.gpio, tenant_id: tenantId } }
      ];

      await Promise.all(
        updates.map(u => fetch(`/api/nodes/${u.id}`, {
          method: 'PATCH',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(u.data)
        }))
      );
      
      onClose();
    } catch (err) {
      console.error('Failed to save configs:', err);
      alert('Failed to save settings');
    } finally {
      setSaving(false);
    }
  };

  if (!isOpen) return null;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4">
      <div className="absolute inset-0 bg-black/80 backdrop-blur-sm" onClick={onClose} />
      
      <div className="relative bg-gray-900 border border-gray-800 rounded-2xl w-full max-w-2xl shadow-2xl overflow-hidden flex flex-col max-h-[90vh]">
        {/* Header */}
        <div className="px-6 py-4 border-b border-gray-800 flex items-center justify-between bg-gray-900/50">
          <div>
            <h2 className="text-xl font-bold text-white flex items-center gap-2">
              <span className="text-blue-500">⚙️</span> Device Settings
            </h2>
            <p className="text-xs text-gray-500 mt-0.5">Configure hardware and AI providers for {tenantId}</p>
          </div>
          <button onClick={onClose} className="p-2 hover:bg-gray-800 rounded-full text-gray-400 hover:text-white transition">
            <svg className="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>

        {/* Tabs */}
        <div className="flex border-b border-gray-800 bg-gray-900/30">
          {(['telegram', 'llm', 'gpio'] as const).map(tab => (
            <button
              key={tab}
              onClick={() => setActiveTab(tab)}
              className={`flex-1 py-3 text-sm font-medium transition ${
                activeTab === tab 
                  ? 'text-blue-400 border-b-2 border-blue-500 bg-blue-500/5' 
                  : 'text-gray-500 hover:text-gray-300 hover:bg-gray-800'
              }`}
            >
              {tab.toUpperCase()}
            </button>
          ))}
        </div>

        {/* Content */}
        <div className="flex-1 overflow-y-auto p-6 space-y-6">
          {loading ? (
            <div className="flex flex-col items-center justify-center py-20 gap-4">
              <div className="w-8 h-8 border-4 border-blue-500 border-t-transparent rounded-full animate-spin" />
              <p className="text-sm text-gray-500">Fetching configuration from cloud...</p>
            </div>
          ) : (
            <>
              {activeTab === 'telegram' && (
                <div className="space-y-4 animate-in fade-in slide-in-from-bottom-2 duration-300">
                  <div>
                    <label className="block text-sm font-medium text-gray-400 mb-2">Bot Token</label>
                    <input
                      type="text"
                      value={config.telegram.bot_token}
                      onChange={e => setConfig({ ...config, telegram: { ...config.telegram, bot_token: e.target.value } })}
                      placeholder="123456789:ABCDefGhIjKlMnOpQrStUvWxYz"
                      className="w-full bg-gray-800 border border-gray-700 rounded-xl px-4 py-3 text-white focus:border-blue-500 focus:outline-none transition"
                    />
                  </div>
                  <div>
                    <label className="block text-sm font-medium text-gray-400 mb-2">Your Chat ID</label>
                    <input
                      type="text"
                      value={config.telegram.chat_id}
                      onChange={e => setConfig({ ...config, telegram: { ...config.telegram, chat_id: e.target.value } })}
                      placeholder="987654321"
                      className="w-full bg-gray-800 border border-gray-700 rounded-xl px-4 py-3 text-white focus:border-blue-500 focus:outline-none transition"
                    />
                    <p className="text-xs text-gray-500 mt-2 italic">Device will send OTP and notifications to this ID.</p>
                  </div>
                </div>
              )}

              {activeTab === 'llm' && (
                <div className="space-y-4 animate-in fade-in slide-in-from-bottom-2 duration-300">
                  <div className="grid grid-cols-2 gap-4">
                    <div>
                      <label className="block text-sm font-medium text-gray-400 mb-2">Provider</label>
                      <select
                        value={config.llm.provider}
                        onChange={e => setConfig({ ...config, llm: { ...config.llm, provider: e.target.value } })}
                        className="w-full bg-gray-800 border border-gray-700 rounded-xl px-4 py-3 text-white focus:border-blue-500 focus:outline-none transition"
                      >
                        <option value="anthropic">Anthropic (Claude)</option>
                        <option value="openai">OpenAI (GPT)</option>
                        <option value="custom">Custom (OpenAI API Compatible)</option>
                      </select>
                    </div>
                    <div>
                      <label className="block text-sm font-medium text-gray-400 mb-2">Model</label>
                      <input
                        type="text"
                        value={config.llm.model}
                        onChange={e => setConfig({ ...config, llm: { ...config.llm, model: e.target.value } })}
                        placeholder="claude-3-haiku-20240307"
                        className="w-full bg-gray-800 border border-gray-700 rounded-xl px-4 py-3 text-white focus:border-blue-500 focus:outline-none transition"
                      />
                    </div>
                  </div>
                  <div>
                    <label className="block text-sm font-medium text-gray-400 mb-2">API Key</label>
                    <input
                      type="password"
                      value={config.llm.api_key}
                      onChange={e => setConfig({ ...config, llm: { ...config.llm, api_key: e.target.value } })}
                      placeholder="sk-..."
                      className="w-full bg-gray-800 border border-gray-700 rounded-xl px-4 py-3 text-white focus:border-blue-500 focus:outline-none transition"
                    />
                  </div>
                  <div>
                    <label className="block text-sm font-medium text-gray-400 mb-2">Base URL (optional)</label>
                    <input
                      type="text"
                      value={config.llm.base_url}
                      onChange={e => setConfig({ ...config, llm: { ...config.llm, base_url: e.target.value } })}
                      placeholder="https://api.anthropic.com"
                      className="w-full bg-gray-800 border border-gray-700 rounded-xl px-4 py-3 text-white focus:border-blue-500 focus:outline-none transition"
                    />
                  </div>
                </div>
              )}

              {activeTab === 'gpio' && (
                <div className="space-y-6 animate-in fade-in slide-in-from-bottom-2 duration-300">
                  <div className="grid grid-cols-2 gap-6">
                    <div className="space-y-4 p-4 bg-gray-800/50 rounded-2xl border border-gray-700">
                      <h3 className="text-xs font-bold text-blue-400 uppercase tracking-widest">OLED (I2C)</h3>
                      <div className="grid grid-cols-2 gap-3">
                        <div>
                          <label className="text-[10px] text-gray-500 block mb-1">SDA</label>
                          <input type="number" value={config.gpio.sda} onChange={e => setConfig({ ...config, gpio: { ...config.gpio, sda: parseInt(e.target.value) } })} className="w-full bg-gray-900 border border-gray-700 rounded-lg px-2 py-1 text-sm text-white" />
                        </div>
                        <div>
                          <label className="text-[10px] text-gray-500 block mb-1">SCL</label>
                          <input type="number" value={config.gpio.scl} onChange={e => setConfig({ ...config, gpio: { ...config.gpio, scl: parseInt(e.target.value) } })} className="w-full bg-gray-900 border border-gray-700 rounded-lg px-2 py-1 text-sm text-white" />
                        </div>
                      </div>
                    </div>
                    <div className="space-y-4 p-4 bg-gray-800/50 rounded-2xl border border-gray-700">
                      <h3 className="text-xs font-bold text-purple-400 uppercase tracking-widest">Microphone (I2S)</h3>
                      <div className="grid grid-cols-3 gap-2">
                        <div>
                          <label className="text-[10px] text-gray-500 block mb-1">SCK</label>
                          <input type="number" value={config.gpio.mic_sck} onChange={e => setConfig({ ...config, gpio: { ...config.gpio, mic_sck: parseInt(e.target.value) } })} className="w-full bg-gray-900 border border-gray-700 rounded-lg px-2 py-1 text-sm text-white" />
                        </div>
                        <div>
                          <label className="text-[10px] text-gray-500 block mb-1">WS</label>
                          <input type="number" value={config.gpio.mic_ws} onChange={e => setConfig({ ...config, gpio: { ...config.gpio, mic_ws: parseInt(e.target.value) } })} className="w-full bg-gray-900 border border-gray-700 rounded-lg px-2 py-1 text-sm text-white" />
                        </div>
                        <div>
                          <label className="text-[10px] text-gray-500 block mb-1">SD</label>
                          <input type="number" value={config.gpio.mic_sd} onChange={e => setConfig({ ...config, gpio: { ...config.gpio, mic_sd: parseInt(e.target.value) } })} className="w-full bg-gray-900 border border-gray-700 rounded-lg px-2 py-1 text-sm text-white" />
                        </div>
                      </div>
                    </div>
                  </div>
                  <div className="p-4 bg-gray-800/50 rounded-2xl border border-gray-700">
                    <h3 className="text-xs font-bold text-green-400 uppercase tracking-widest mb-4">Speaker (I2S)</h3>
                    <div className="grid grid-cols-3 gap-4">
                      <div>
                        <label className="text-[10px] text-gray-500 block mb-1">BCLK</label>
                        <input type="number" value={config.gpio.spk_bclk} onChange={e => setConfig({ ...config, gpio: { ...config.gpio, spk_bclk: parseInt(e.target.value) } })} className="w-full bg-gray-900 border border-gray-700 rounded-lg px-2 py-1 text-sm text-white" />
                      </div>
                      <div>
                        <label className="text-[10px] text-gray-500 block mb-1">LRC</label>
                        <input type="number" value={config.gpio.spk_lrc} onChange={e => setConfig({ ...config, gpio: { ...config.gpio, spk_lrc: parseInt(e.target.value) } })} className="w-full bg-gray-900 border border-gray-700 rounded-lg px-2 py-1 text-sm text-white" />
                      </div>
                      <div>
                        <label className="text-[10px] text-gray-500 block mb-1">DOUT</label>
                        <input type="number" value={config.gpio.spk_dout} onChange={e => setConfig({ ...config, gpio: { ...config.gpio, spk_dout: parseInt(e.target.value) } })} className="w-full bg-gray-900 border border-gray-700 rounded-lg px-2 py-1 text-sm text-white" />
                      </div>
                    </div>
                  </div>
                </div>
              )}
            </>
          )}
        </div>

        {/* Footer */}
        <div className="px-6 py-4 border-t border-gray-800 flex gap-3 bg-gray-900/50">
          <button
            onClick={onClose}
            className="flex-1 py-3 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-xl font-semibold transition"
          >
            Cancel
          </button>
          <button
            onClick={handleSave}
            disabled={saving || loading}
            className="flex-[2] py-3 bg-gradient-to-r from-blue-600 to-purple-600 text-white rounded-xl font-bold shadow-lg shadow-blue-500/20 hover:opacity-90 transition disabled:opacity-50"
          >
            {saving ? 'Saving...' : 'Apply Changes to Cloud'}
          </button>
        </div>
      </div>
    </div>
  );
}
