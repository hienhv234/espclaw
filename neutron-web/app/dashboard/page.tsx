'use client';

import { useEffect, useState, useCallback, useRef } from 'react';
import dynamic from 'next/dynamic';
import { getSupabaseClient } from '@/lib/supabase';
import { ESP32_TOOLS, WORKFLOW_TEMPLATES, getTool, TOOL_CATEGORIES } from '@/lib/esp32-tools';
import { GraphNode, GraphLink } from '@/lib/graph';
import SettingsModal from '@/components/SettingsModal';

const ForceGraph3D = dynamic(() => import('react-force-graph-3d'), { ssr: false });

// ── Types ────────────────────────────────────────────────────────────────

interface Workflow {
  id: string;
  name: string;
  icon: string;
  category: string;
  description?: string;
  trigger_type: string;
  trigger_pattern: string;
  trigger_node_id?: string;
  steps: WorkflowStep[];
  is_enabled: boolean;
  priority: number;
  trigger_count: number;
  success_count: number;
  version: number;
  updated_at: string;
}

interface WorkflowStep {
  id: number;
  type: 'tool' | 'wait';
  tool?: string;
  params?: Record<string, unknown>;
  milliseconds?: number;
  description?: string;
}

interface Execution {
  id: string;
  workflow_id: string;
  status: string;
  started_at: string;
  duration_ms: number;
  steps_executed: Array<{ step: number; type: string; name: string; result: string; ms: number; success: boolean }>;
  error_message?: string;
}

interface GraphNode2D {
  id: string;
  name: string;
  type: string;
  subtype?: string;
  pos_x: number;
  pos_y: number;
  pos_z: number;
  pulse_strength?: number;
}

interface Link2D {
  id: string;
  source: string;
  target: string;
  type: string;
  weight: number;
}

// ── Helper: Node icon ───────────────────────────────────────────────────

function getNodeIcon(type: string): string {
  const map: Record<string, string> = {
    user: '👤', device: '📱', skill: '⚡', memory: '🧠', tag: '🏷️',
    transaction: '💸', entity: '🔮', event: '📅', goal: '🎯',
    routine: '🔄', insight: '💡', pulse: '🌊', context: '🎭',
  };
  return map[type] || '⬡';
}

function getNodeColor(type: string): number {
  const map: Record<string, number> = {
    user: 0x4ade80, device: 0x60a5fa, skill: 0xfacc15, memory: 0xc084fc,
    tag: 0xf87171, entity: 0x2dd4bf, event: 0xfb923c, goal: 0xe879f9,
  };
  return map[type] || 0x888888;
}

// ── Workflow Editor Panel ───────────────────────────────────────────────

interface WorkflowEditorProps {
  workflow: Workflow | null;
  isNew: boolean;
  onSave: (w: Partial<Workflow>) => Promise<void>;
  onClose: () => void;
  onDelete: (id: string) => Promise<void>;
  onTest: (id: string) => void;
  onToggle: (id: string, enabled: boolean) => void;
  nodes: GraphNode2D[];
}

function WorkflowEditor({ workflow, isNew, onSave, onClose, onDelete, onTest, onToggle, nodes }: WorkflowEditorProps) {
  const [form, setForm] = useState<Partial<Workflow>>({});
  const [steps, setSteps] = useState<WorkflowStep[]>([]);
  const [saving, setSaving] = useState(false);
  const [showToolPicker, setShowToolPicker] = useState(false);

  useEffect(() => {
    if (workflow) {
      setForm({ ...workflow });
      setSteps(workflow.steps || []);
    } else {
      setForm({
        name: '',
        icon: '⚡',
        category: 'automation',
        trigger_type: 'voice_command',
        trigger_pattern: '',
        trigger_node_id: undefined,
        steps: [],
        is_enabled: true,
        priority: 50,
      });
      setSteps([]);
    }
  }, [workflow]);

  function addStep(type: 'tool' | 'wait') {
    const newStep: WorkflowStep = {
      id: steps.length,
      type,
      description: '',
      ...(type === 'tool' ? { tool: 'gpio_write', params: { pin: 2, state: 1 } } : { milliseconds: 500 }),
    };
    setSteps([...steps, newStep]);
  }

  function updateStep(index: number, updates: Partial<WorkflowStep>) {
    setSteps(steps.map((s, i) => i === index ? { ...s, ...updates } : s));
  }

  function removeStep(index: number) {
    setSteps(steps.map((s, i) => i > index ? { ...s, id: s.id - 1 } : s).filter((_, i) => i !== index));
  }

  async function handleSave() {
    if (!form.name?.trim()) return;
    setSaving(true);
    await onSave({ ...form, steps });
    setSaving(false);
  }

  const templateOptions = [
    { id: 'led_blink', label: 'LED Blink 💡', icon: '💡' },
    { id: 'gpio_blink', label: 'LED Blink 💡', icon: '💡' },
    { id: 'wifi_status', label: 'WiFi Status 📡', icon: '📡' },
    { id: 'morning_routine', label: 'Morning Routine ☀️', icon: '☀️' },
    { id: 'memory_stats', label: 'Memory Stats 📊', icon: '📊' },
  ];

  function applyTemplate(templateId: string) {
    const tmpl = WORKFLOW_TEMPLATES.find(x => x.id === templateId);
    if (!tmpl) return;
    setForm(f => ({ ...f, name: tmpl.name, icon: tmpl.icon, category: tmpl.category, trigger_pattern: tmpl.trigger_pattern }));
    setSteps(tmpl.steps.map((s: any) => ({ ...s })));
  }

  return (
    <div className="h-full flex flex-col bg-gray-900 border-l border-white/10 overflow-hidden">
      {/* Header */}
      <div className="flex items-center justify-between px-4 py-3 border-b border-white/10">
        <h2 className="font-semibold text-gray-200">
          {isNew ? '➕ New Workflow' : `⚙️ ${workflow?.icon || '⚡'} ${workflow?.name}`}
        </h2>
        <button onClick={onClose} className="text-gray-500 hover:text-white text-xl leading-none">×</button>
      </div>

      <div className="flex-1 overflow-y-auto p-4 space-y-4">
        {/* Quick Templates */}
        {isNew && (
          <div>
            <label className="text-xs font-medium text-gray-400 uppercase tracking-wider mb-2 block">Templates</label>
            <div className="grid grid-cols-2 gap-2">
              {templateOptions.map(t => (
                <button
                  key={t.id}
                  onClick={() => applyTemplate(t.id)}
                  className="text-xs p-2 rounded-lg bg-gray-800 border border-white/5 hover:border-white/20 text-gray-300 text-left transition"
                >
                  {t.label}
                </button>
              ))}
            </div>
          </div>
        )}

        {/* Basic Info */}
        <div className="space-y-3">
          <label className="text-xs font-medium text-gray-400 uppercase tracking-wider mb-2 block">Basic Info</label>

          <div className="flex gap-2">
            <input
              value={form.icon || '⚡'}
              onChange={e => setForm({ ...form, icon: e.target.value })}
              className="w-12 text-center bg-gray-800 border border-white/10 rounded-lg text-lg"
              maxLength={2}
            />
            <input
              value={form.name || ''}
              onChange={e => setForm({ ...form, name: e.target.value })}
              placeholder="Workflow name"
              className="flex-1 bg-gray-800 border border-white/10 rounded-lg px-3 text-sm text-white placeholder-gray-500 focus:border-purple-500 focus:outline-none"
            />
          </div>

          <input
            value={form.description || ''}
            onChange={e => setForm({ ...form, description: e.target.value })}
            placeholder="Description (optional)"
            className="w-full bg-gray-800 border border-white/10 rounded-lg px-3 py-2 text-sm text-white placeholder-gray-500 focus:border-purple-500 focus:outline-none"
          />

          <div className="grid grid-cols-2 gap-2">
            <div>
              <label className="text-xs text-gray-500 mb-1 block">Category</label>
              <select
                value={form.category || 'automation'}
                onChange={e => setForm({ ...form, category: e.target.value })}
                className="w-full bg-gray-800 border border-white/10 rounded-lg px-2 py-1.5 text-sm text-white focus:border-purple-500 focus:outline-none"
              >
                {['automation', 'monitoring', 'finance', 'health', 'home', 'social', 'general'].map(c => (
                  <option key={c} value={c}>{c}</option>
                ))}
              </select>
            </div>
            <div>
              <label className="text-xs text-gray-500 mb-1 block">Priority</label>
              <input
                type="number"
                min={1}
                max={100}
                value={form.priority || 50}
                onChange={e => setForm({ ...form, priority: parseInt(e.target.value) })}
                className="w-full bg-gray-800 border border-white/10 rounded-lg px-2 py-1.5 text-sm text-white focus:border-purple-500 focus:outline-none"
              />
            </div>
          </div>
        </div>

        {/* Trigger */}
        <div>
          <label className="text-xs font-medium text-gray-400 uppercase tracking-wider mb-2 block">Trigger</label>
          <div className="space-y-2">
            <div className="flex gap-2">
              <span className="text-xs text-gray-500 bg-gray-800 rounded px-2 py-1.5 flex items-center">Voice</span>
              <input
                value={form.trigger_pattern || ''}
                onChange={e => setForm({ ...form, trigger_pattern: e.target.value })}
                placeholder="e.g. bật đèn, check wifi, chi tiêu"
                className="flex-1 bg-gray-800 border border-white/10 rounded-lg px-3 text-sm text-white placeholder-gray-500 focus:border-purple-500 focus:outline-none"
              />
            </div>
            <p className="text-xs text-gray-500">
              Khi người dùng nói hoặc gửi tin nhắn chứa text này, workflow sẽ chạy.
              <span className="text-gray-600 ml-1">(không phân biệt hoa thường)</span>
            </p>
          </div>
        </div>

        {/* Steps */}
        <div>
          <div className="flex items-center justify-between mb-2">
            <label className="text-xs font-medium text-gray-400 uppercase tracking-wider">Steps</label>
            <div className="flex gap-2">
              <button
                onClick={() => addStep('tool')}
                className="text-xs px-2 py-1 rounded bg-blue-600/20 text-blue-400 hover:bg-blue-600/30 transition"
              >
                + Tool
              </button>
              <button
                onClick={() => addStep('wait')}
                className="text-xs px-2 py-1 rounded bg-gray-600/20 text-gray-400 hover:bg-gray-600/30 transition"
              >
                + Wait
              </button>
            </div>
          </div>

          <div className="space-y-2">
            {steps.length === 0 && (
              <div className="text-center py-8 text-gray-500 text-sm border border-dashed border-white/10 rounded-lg">
                Chưa có step nào. Nhấn <span className="text-blue-400">+ Tool</span> để thêm.
              </div>
            )}

            {steps.map((step, idx) => {
              const tool = step.tool ? getTool(step.tool) : null;
              return (
                <div key={idx} className="bg-gray-800/60 rounded-lg border border-white/5 p-3">
                  <div className="flex items-start gap-2">
                    <span className="w-5 h-5 rounded bg-purple-600/30 text-purple-400 text-xs flex items-center justify-center flex-shrink-0 mt-0.5">
                      {idx}
                    </span>
                    <div className="flex-1 min-w-0">
                      {step.type === 'tool' ? (
                        <div className="space-y-2">
                          <div className="flex gap-2">
                            <select
                              value={step.tool || 'gpio_write'}
                              onChange={e => {
                                const t = getTool(e.target.value);
                                updateStep(idx, {
                                  tool: e.target.value,
                                  params: t?.example || {},
                                  description: t?.description,
                                });
                              }}
                              className="flex-1 bg-gray-700 border border-white/10 rounded px-2 py-1 text-xs text-white focus:border-purple-500 focus:outline-none"
                            >
                              {ESP32_TOOLS.map(t => (
                                <option key={t.name} value={t.name}>
                                  {TOOL_CATEGORIES[t.category]?.icon || '⚙️'} {t.name}
                                </option>
                              ))}
                            </select>
                            <button onClick={() => removeStep(idx)} className="text-gray-500 hover:text-red-400 text-sm px-1">✕</button>
                          </div>

                          {/* Tool params */}
                          {tool && tool.parameters.length > 0 && (
                            <div className="space-y-1 pl-2 border-l border-white/10">
                              {tool.parameters.map(param => (
                                <div key={param.name} className="flex items-center gap-2">
                                  <span className="text-xs text-gray-500 w-20 flex-shrink-0">{param.name}</span>
                                  {param.enum ? (
                                    <select
                                      value={String((step.params || {})[param.name] || '')}
                                      onChange={e => updateStep(idx, { params: { ...step.params, [param.name]: param.type === 'integer' ? parseInt(e.target.value) : e.target.value } })}
                                      className="flex-1 bg-gray-700 border border-white/10 rounded px-2 py-0.5 text-xs text-white"
                                    >
                                      {param.enum.map(v => <option key={v} value={v}>{v}</option>)}
                                    </select>
                                  ) : (
                                    <input
                                      type={param.type === 'integer' ? 'number' : 'text'}
                                      value={String((step.params || {})[param.name] || '')}
                                      onChange={e => updateStep(idx, { params: { ...step.params, [param.name]: param.type === 'integer' ? parseInt(e.target.value) : e.target.value } })}
                                      className="flex-1 bg-gray-700 border border-white/10 rounded px-2 py-0.5 text-xs text-white"
                                      placeholder={param.description}
                                    />
                                  )}
                                </div>
                              ))}
                            </div>
                          )}

                          <input
                            value={step.description || ''}
                            onChange={e => updateStep(idx, { description: e.target.value })}
                            placeholder="Mô tả step (VD: Bật đèn phòng khách)"
                            className="w-full bg-gray-700/50 border border-white/5 rounded px-2 py-1 text-xs text-gray-300 placeholder-gray-500 focus:border-purple-500 focus:outline-none"
                          />
                        </div>
                      ) : (
                        <div className="space-y-2">
                          <div className="flex gap-2 items-center">
                            <span className="text-xs text-gray-400">⏱️ Delay</span>
                            <input
                              type="number"
                              value={step.milliseconds || 500}
                              onChange={e => updateStep(idx, { milliseconds: parseInt(e.target.value) })}
                              className="w-20 bg-gray-700 border border-white/10 rounded px-2 py-0.5 text-xs text-white"
                              min={100}
                              max={60000}
                            />
                            <span className="text-xs text-gray-500">ms</span>
                            <button onClick={() => removeStep(idx)} className="text-gray-500 hover:text-red-400 text-sm ml-auto px-1">✕</button>
                          </div>
                          <input
                            value={step.description || ''}
                            onChange={e => updateStep(idx, { description: e.target.value })}
                            placeholder="Mô tả (VD: Chờ đèn nhấp nháy)"
                            className="w-full bg-gray-700/50 border border-white/5 rounded px-2 py-1 text-xs text-gray-300 placeholder-gray-500 focus:border-purple-500 focus:outline-none"
                          />
                        </div>
                      )}
                    </div>
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      </div>

      {/* Actions */}
      <div className="border-t border-white/10 p-4 space-y-2">
        <button
          onClick={handleSave}
          disabled={saving || !form.name?.trim()}
          className="w-full py-2 rounded-lg font-medium text-sm transition disabled:opacity-50 disabled:cursor-not-allowed bg-purple-600 hover:bg-purple-500 text-white"
        >
          {saving ? '💾 Đang lưu...' : isNew ? '✨ Tạo Workflow' : '💾 Lưu thay đổi'}
        </button>

        {!isNew && workflow && (
          <div className="flex gap-2">
            <button
              onClick={() => onTest(workflow.id)}
              className="flex-1 py-2 rounded-lg font-medium text-sm transition bg-blue-600/20 hover:bg-blue-600/30 text-blue-400"
            >
              🧪 Test
            </button>
            <button
              onClick={() => onToggle(workflow.id, !workflow.is_enabled)}
              className={`flex-1 py-2 rounded-lg font-medium text-sm transition ${
                workflow.is_enabled ? 'bg-yellow-600/20 text-yellow-400 hover:bg-yellow-600/30' : 'bg-green-600/20 text-green-400 hover:bg-green-600/30'
              }`}
            >
              {workflow.is_enabled ? '⏸ Tắt' : '▶ Bật'}
            </button>
            <button
              onClick={() => { if (confirm('Xóa workflow này?')) onDelete(workflow.id); }}
              className="py-2 px-3 rounded-lg font-medium text-sm transition bg-red-600/20 hover:bg-red-600/30 text-red-400"
            >
              🗑
            </button>
          </div>
        )}
      </div>
    </div>
  );
}

// ── Debug Console ────────────────────────────────────────────────────────

interface DebugConsoleProps {
  executions: Execution[];
  pulses: Array<{ id: string; type: string; text: string; created_at: string }>;
  onClear: () => void;
}

function DebugConsole({ executions, pulses, onClear }: DebugConsoleProps) {
  const [activeTab, setActiveTab] = useState<'executions' | 'pulses'>('executions');

  return (
    <div className="bg-gray-900 border-t border-white/10">
      <div className="flex items-center justify-between px-4 py-2 border-b border-white/5">
        <div className="flex gap-1">
          {(['executions', 'pulses'] as const).map(tab => (
            <button
              key={tab}
              onClick={() => setActiveTab(tab)}
              className={`px-3 py-1 text-xs rounded-md transition ${
                activeTab === tab ? 'bg-purple-600 text-white' : 'text-gray-400 hover:text-white'
              }`}
            >
              {tab === 'executions' ? '⚡ Executions' : '🌊 Pulses'}
              {tab === 'executions' && executions.length > 0 && (
                <span className="ml-1 text-purple-300">({executions.length})</span>
              )}
            </button>
          ))}
        </div>
        <button onClick={onClear} className="text-xs text-gray-500 hover:text-gray-300">Clear</button>
      </div>

      <div className="h-40 overflow-y-auto p-2 font-mono text-xs">
        {activeTab === 'executions' ? (
          executions.length === 0 ? (
            <div className="text-gray-600 text-center py-4">No executions yet</div>
          ) : (
            executions.map(exec => (
              <div key={exec.id} className="mb-2 pb-2 border-b border-white/5">
                <div className="flex items-center gap-2 mb-1">
                  <span className={`w-2 h-2 rounded-full ${exec.status === 'completed' ? 'bg-green-400' : exec.status === 'failed' ? 'bg-red-400' : 'bg-yellow-400'}`} />
                  <span className="text-gray-300">{exec.workflow_id.slice(0, 8)}</span>
                  <span className="text-gray-500">{exec.duration_ms}ms</span>
                  <span className="text-gray-600">{new Date(exec.started_at).toLocaleTimeString()}</span>
                </div>
                {exec.steps_executed.map((s, i) => (
                  <div key={i} className="pl-4 text-gray-400">
                    {s.success ? '✓' : '✗'} [{s.step}] {s.name}: <span className="text-gray-500">{s.result}</span>
                  </div>
                ))}
                {exec.error_message && (
                  <div className="pl-4 text-red-400">Error: {exec.error_message}</div>
                )}
              </div>
            ))
          )
        ) : (
          pulses.length === 0 ? (
            <div className="text-gray-600 text-center py-4">No pulses</div>
          ) : (
            pulses.map(pulse => (
              <div key={pulse.id} className="flex items-start gap-2 py-1 border-b border-white/5">
                <span className="text-purple-400 flex-shrink-0">{pulse.type === 'action' ? '⚡' : pulse.type === 'perception' ? '👁' : '💭'}</span>
                <span className="text-gray-300 truncate flex-1">{pulse.text}</span>
                <span className="text-gray-600 flex-shrink-0">{new Date(pulse.created_at).toLocaleTimeString()}</span>
              </div>
            ))
          )
        )}
      </div>
    </div>
  );
}

// ── Main Dashboard ──────────────────────────────────────────────────────

export default function WorkflowDashboard() {
  const [workflows, setWorkflows] = useState<Workflow[]>([]);
  const [nodes, setNodes] = useState<GraphNode2D[]>([]);
  const [links, setLinks] = useState<Link2D[]>([]);
  const [executions, setExecutions] = useState<Execution[]>([]);
  const [recentPulses, setRecentPulses] = useState<Array<{ id: string; type: string; text: string; created_at: string }>>([]);
  const [loading, setLoading] = useState(true);
  const [selectedWorkflow, setSelectedWorkflow] = useState<Workflow | null>(null);
  const [isNewWorkflow, setIsNewWorkflow] = useState(false);
  const [editorOpen, setEditorOpen] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [tenantId, setTenantId] = useState('');
  const [testResult, setTestResult] = useState<Record<string, unknown> | null>(null);
  const supabase = getSupabaseClient();
  const graphRef = useRef<any>(null);

  const loadData = useCallback(async () => {
    const stored = localStorage.getItem('espclaw_device');
    if (!stored) { window.location.href = '/'; return; }
    const deviceData = JSON.parse(stored);
    const tId = (deviceData as { tenant_id?: string }).tenant_id;
    if (!tId) { setLoading(false); return; }
    setTenantId(tId);

    const [wfRes, treeRes, pulsesRes] = await Promise.all([
      fetch('/api/workflows?tenant_id=' + tenantId).then(r => r.json()),
      fetch('/api/graph/workflow-tree?tenant_id=' + tenantId + '&type=tree').then(r => r.json()),
      supabase.from('pulses').select('*').eq('tenant_id', tenantId).order('created_at', { ascending: false }).limit(30),
    ]);

    setWorkflows(wfRes.workflows || []);
    setNodes(treeRes.nodes || []);
    setLinks(treeRes.links || []);
    setRecentPulses(pulsesRes.data || []);
    setLoading(false);
  }, [supabase]);

  useEffect(() => { loadData(); }, [loadData]);

  // Realtime subscription
  useEffect(() => {
    const channel = supabase
      .channel('dashboard-realtime')
      .on('postgres_changes', { event: '*', schema: 'public', table: 'pulses' }, loadData)
      .on('postgres_changes', { event: '*', schema: 'public', table: 'workflow_executions' }, loadData)
      .subscribe();
    return () => { supabase.removeChannel(channel); };
  }, [loadData, supabase]);

  async function handleSave(updates: Partial<Workflow>) {
    const stored = localStorage.getItem('espclaw_device');
    if (!stored) return;
    const deviceData = JSON.parse(stored);
    const tenantId = (deviceData as { tenant_id?: string }).tenant_id;
    if (!tenantId) return;

    if (isNewWorkflow) {
      const res = await fetch('/api/workflows', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ tenant_id: tenantId, ...updates }),
      });
      if (res.ok) {
        setIsNewWorkflow(false);
        setSelectedWorkflow(null);
        setEditorOpen(false);
        await loadData();
      }
    } else if (selectedWorkflow) {
      const res = await fetch(`/api/workflows/${selectedWorkflow.id}`, {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(updates),
      });
      if (res.ok) {
        await loadData();
      }
    }
  }

  async function handleDelete(id: string) {
    await fetch(`/api/workflows/${id}`, { method: 'DELETE' });
    setSelectedWorkflow(null);
    setEditorOpen(false);
    await loadData();
  }

  async function handleToggle(id: string, enabled: boolean) {
    await fetch(`/api/workflows/${id}`, {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ is_enabled: enabled }),
    });
    await loadData();
  }

  async function handleTest(id: string) {
    const stored = localStorage.getItem('espclaw_device');
    if (!stored) return;
    const deviceData = JSON.parse(stored);
    const tenantId = (deviceData as { tenant_id?: string }).tenant_id;

    const res = await fetch(`/api/workflows/${id}/execute`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ tenant_id: tenantId, dry_run: true }),
    });
    const data = await res.json();
    setTestResult(data);

    if (res.ok) {
      const wf = workflows.find(w => w.id === id);
      if (wf) {
        const execRes = await fetch(`/api/workflows/${id}/executions?limit=1`);
        const execData = await execRes.json();
        setExecutions(prev => [(execData.executions?.[0] || { id, workflow_id: id, status: 'completed', started_at: new Date().toISOString(), duration_ms: data.duration_ms, steps_executed: data.results || [] }), ...prev].slice(0, 50));
      }
    }
  }

  const graphData = {
    nodes: nodes.map(n => ({ ...n, x: (n.pos_x || 50) * 10 - 500, y: (n.pos_y || 500) - 500, z: (n.pos_z || 50) * 10 - 500 })),
    links: links.map(l => ({ ...l, source: l.source, target: l.target })),
  };

  async function handleLogout() {
    localStorage.removeItem('espclaw_device');
    window.location.href = '/';
  }

  async function handleSync() {
    const stored = localStorage.getItem('espclaw_device');
    if (!stored) return;
    const deviceData = JSON.parse(stored);
    
    try {
      const res = await fetch('/api/device/config-pull', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ deviceId: deviceData.device_id })
      });
      if (res.ok) {
        alert('Pull config command sent to device. Cloud config will be applied only if local ESP is empty.');
      } else {
        alert('Failed to send pull config command');
      }
    } catch (err) {
      console.error(err);
      alert('Error sending pull config command');
    }
  }

  if (loading) {
    return (
      <div className="min-h-screen bg-gray-950 flex items-center justify-center">
        <div className="text-center">
          <div className="text-5xl mb-4 animate-pulse">⚡</div>
          <p className="text-gray-400">Đang tải workflow engine...</p>
        </div>
      </div>
    );
  }

  return (
    <div className="min-h-screen bg-gray-950 text-white flex flex-col">
      {/* Header */}
      <header className="border-b border-white/5 bg-gray-900/80 backdrop-blur sticky top-0 z-20">
        <div className="max-w-7xl mx-auto px-6 py-3 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <span className="text-2xl">⚡</span>
            <div>
              <h1 className="text-lg font-bold">Workflow Engine</h1>
              <p className="text-xs text-gray-400">
                {workflows.length} workflows • {nodes.length} nodes • {executions.length} executions
              </p>
            </div>
          </div>
          <div className="flex items-center gap-2">
            <button
              onClick={() => { setIsNewWorkflow(true); setSelectedWorkflow(null); setEditorOpen(true); }}
              className="px-4 py-2 text-sm bg-purple-600 hover:bg-purple-500 rounded-lg font-medium transition flex items-center gap-2"
            >
              ➕ New Workflow
            </button>
            <a
              href="/dashboard/wakeword"
              className="px-4 py-2 text-sm bg-emerald-600/20 hover:bg-emerald-600/30 rounded-lg text-emerald-400 transition flex items-center gap-2"
            >
              🎤 Wake Word
            </a>
            <button 
              onClick={() => setSettingsOpen(true)}
              className="px-4 py-2 text-sm bg-gray-800 hover:bg-gray-700 rounded-lg text-gray-300 transition flex items-center gap-2"
            >
              ⚙️ Settings
            </button>
            <button 
              onClick={handleSync}
              className="px-4 py-2 text-sm bg-blue-600/20 hover:bg-blue-600/30 rounded-lg text-blue-400 transition flex items-center gap-2"
              title="Pull cloud config to ESP — only if local ESP is empty"
            >
              ☁️ Pull from Cloud
            </button>
            <button 
              onClick={handleLogout}
              className="px-4 py-2 text-sm bg-red-600/20 hover:bg-red-600/30 rounded-lg text-red-400 transition flex items-center gap-2"
            >
              🚪 Logout
            </button>
            <button onClick={loadData} className="px-3 py-2 text-sm bg-gray-800 hover:bg-gray-700 rounded-lg text-gray-300 transition">
              🔄
            </button>
          </div>
        </div>
      </header>

      {/* Main Layout */}
      <div className="flex-1 flex overflow-hidden">
        {/* Left: Workflow List */}
        <div className="w-72 border-r border-white/10 flex flex-col overflow-hidden">
          <div className="p-3 border-b border-white/5">
            <input
              placeholder="Tìm workflow..."
              className="w-full bg-gray-800 border border-white/10 rounded-lg px-3 py-1.5 text-sm text-white placeholder-gray-500 focus:border-purple-500 focus:outline-none"
            />
          </div>

          <div className="flex-1 overflow-y-auto">
            {workflows.length === 0 ? (
              <div className="text-center py-12 px-4">
                <div className="text-4xl mb-3">🧩</div>
                <p className="text-sm text-gray-400 mb-3">Chưa có workflow nào</p>
                <button
                  onClick={() => { setIsNewWorkflow(true); setSelectedWorkflow(null); setEditorOpen(true); }}
                  className="text-sm text-purple-400 hover:text-purple-300"
                >
                  + Tạo workflow đầu tiên
                </button>
              </div>
            ) : (
              workflows.map(w => (
                <button
                  key={w.id}
                  onClick={() => { setSelectedWorkflow(w); setIsNewWorkflow(false); setEditorOpen(true); }}
                  className={`w-full text-left p-3 border-b border-white/5 transition ${
                    selectedWorkflow?.id === w.id ? 'bg-purple-600/10 border-l-2 border-l-purple-500' : 'hover:bg-gray-800/60'
                  }`}
                >
                  <div className="flex items-center gap-2 mb-1">
                    <span className="text-lg">{w.icon || '⚡'}</span>
                    <span className="font-medium text-sm text-gray-200 truncate">{w.name}</span>
                    {!w.is_enabled && <span className="text-xs text-gray-500">⏸</span>}
                  </div>
                  <div className="flex items-center gap-3 text-xs text-gray-500">
                    <span className="capitalize">{w.category}</span>
                    <span>{w.steps?.length || 0} steps</span>
                    <span>{w.trigger_count || 0} runs</span>
                  </div>
                  {w.trigger_pattern && (
                    <div className="mt-1 text-xs text-gray-600 font-mono truncate">
                      "{w.trigger_pattern}"
                    </div>
                  )}
                </button>
              ))
            )}
          </div>

          {/* Stats */}
          <div className="border-t border-white/5 p-3 space-y-1 text-xs">
            <div className="flex justify-between text-gray-400">
              <span>Enabled</span>
              <span className="text-green-400">{workflows.filter(w => w.is_enabled).length}</span>
            </div>
            <div className="flex justify-between text-gray-400">
              <span>Total runs</span>
              <span className="text-purple-400">{workflows.reduce((s, w) => s + (w.trigger_count || 0), 0)}</span>
            </div>
            <div className="flex justify-between text-gray-400">
              <span>Success rate</span>
              <span className="text-blue-400">
                {(() => {
                  const total = workflows.reduce((s, w) => s + (w.trigger_count || 0), 0);
                  const success = workflows.reduce((s, w) => s + (w.success_count || 0), 0);
                  return total > 0 ? Math.round(success / total * 100) + '%' : '—';
                })()}
              </span>
            </div>
          </div>
        </div>

        {/* Center: 3D Graph */}
        <div className="flex-1 flex flex-col">
          <div className="flex-1 relative" id="graph-container">
            {nodes.length > 0 ? (
              <ForceGraph3D
                ref={graphRef}
                graphData={graphData}
                backgroundColor="#0a0a0f"
                nodeLabel={(node: any) => `<div class="bg-gray-900 px-3 py-2 rounded-lg border border-white/20 text-white text-sm"><b>${node.name}</b><br/><span class="text-xs text-gray-400">${node.type}</span></div>`}
                nodeThreeObject={(node: any) => {
                  const THREE = (window as any).THREE;
                  const size = 6 + (node.pulse_strength || 0) * 10;
                  const color = getNodeColor(node.type);
                  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ color, transparent: true, opacity: 0.8 }));
                  sprite.scale.set(size * 2, size * 2, 1);
                  return sprite;
                }}
                onNodeClick={(node: any) => {
                  const wf = workflows.find(w => w.id === node.id);
                  if (wf) { setSelectedWorkflow(wf); setIsNewWorkflow(false); setEditorOpen(true); }
                }}
                nodeThreeObjectExtend={false}
                linkDirectionalParticles={2}
                linkDirectionalParticleSpeed={0.005}
                linkColor={() => '#334466'}
                linkWidth={1}
                d3VelocityDecay={0.3}
                warmupTicks={50}
              />
            ) : (
              <div className="flex items-center justify-center h-full text-center">
                <div>
                  <div className="text-6xl mb-4 opacity-20">🪐</div>
                  <p className="text-gray-500">3D graph sẽ hiển thị ở đây</p>
                  <p className="text-gray-600 text-sm mt-1">Tạo workflow để bắt đầu</p>
                </div>
              </div>
            )}
          </div>

          {/* Debug Console */}
          <DebugConsole
            executions={executions}
            pulses={recentPulses}
            onClear={() => { setExecutions([]); setRecentPulses([]); }}
          />
        </div>

        {/* Right: Workflow Editor */}
        {editorOpen && (
          <div className="w-96 flex-shrink-0">
            <WorkflowEditor
              workflow={isNewWorkflow ? null : selectedWorkflow}
              isNew={isNewWorkflow}
              onSave={handleSave}
              onClose={() => { setEditorOpen(false); setSelectedWorkflow(null); setIsNewWorkflow(false); }}
              onDelete={handleDelete}
              onTest={handleTest}
              onToggle={handleToggle}
              nodes={nodes}
            />
          </div>
        )}
      </div>

      {/* Test Result Toast */}
      {testResult && (
        <div className="fixed bottom-4 right-4 w-96 bg-gray-900 border border-white/20 rounded-xl shadow-2xl z-50">
          <div className="flex items-center justify-between px-4 py-3 border-b border-white/10">
            <span className="font-medium text-sm">🧪 Test Result</span>
            <button onClick={() => setTestResult(null)} className="text-gray-500 hover:text-white">×</button>
          </div>
          <div className="p-4 font-mono text-xs space-y-1 max-h-64 overflow-y-auto">
            {testResult.success !== undefined && (
              <div className={`text-xs px-2 py-1 rounded mb-2 inline-block ${testResult.success ? 'bg-green-600/20 text-green-400' : 'bg-red-600/20 text-red-400'}`}>
                {testResult.success ? '✅ Success' : '❌ Failed'}
              </div>
            )}
            {testResult.output && (
              <div className="text-gray-300 whitespace-pre-wrap">{String((testResult as any).output ?? '')}</div>
            )}
            {testResult.error && (
              <div className="text-red-400">{String((testResult as any).error ?? '')}</div>
            )}
          </div>
        </div>
      )}

      {/* Settings Modal */}
      <SettingsModal
        isOpen={settingsOpen}
        onClose={() => setSettingsOpen(false)}
        tenantId={tenantId}
      />
    </div>
  );
}
