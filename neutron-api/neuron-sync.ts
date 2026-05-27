/**
 * Neuron Link API - Real-time sync between ESP32 and Cloud
 * 
 * This module provides REST API endpoints for:
 * - Device pairing (6-digit code)
 * - Delta sync (incremental updates)
 * - Full sync (initial device bootstrap)
 * - Pulse recording (real-time events)
 */

import { createClient } from '@supabase/supabase-js';

// Supabase client for server-side operations
const supabase = createClient(
  process.env.NEXT_PUBLIC_SUPABASE_URL!,
  process.env.SUPABASE_SERVICE_ROLE_KEY!,
  {
    auth: {
      autoRefreshToken: false,
      persistSession: false
    }
  }
);

// ============================================================
// Types
// ============================================================

interface NodeDelta {
  id: string;
  type: string;
  name: string;
  content: Record<string, any>;
  pos_x: number;
  pos_y: number;
  pos_z: number;
  is_active: boolean;
  is_pinned: boolean;
  is_favorite: boolean;
  version: number;
  operation: 'upsert' | 'delete';
}

interface LinkDelta {
  id: string;
  source_node_id: string;
  target_node_id: string;
  type: string;
  weight: number;
  confidence: number;
  is_active: boolean;
  version: number;
  operation: 'upsert' | 'delete';
}

interface DeltaSyncRequest {
  deviceId: string;
  deviceSecret: string;
  sinceVersion: number;
  nodes: NodeDelta[];
  links: LinkDelta[];
}

interface DeltaSyncResponse {
  success: boolean;
  serverVersion: number;
  nodes: NodeDelta[];
  links: LinkDelta[];
  deletedNodeIds: string[];
  deletedLinkIds: string[];
  error?: string;
}

interface FullSyncRequest {
  deviceId: string;
  deviceSecret: string;
  deviceType: string;
  firmwareVersion: string;
}

interface FullSyncResponse {
  success: boolean;
  serverVersion: number;
  nodes: NodeDelta[];
  links: LinkDelta[];
  sessionId?: string;
  error?: string;
}

interface PulseRequest {
  deviceId: string;
  sessionId?: string;
  pulseType: 'input' | 'output' | 'tool_call' | 'state_change';
  content: Record<string, any>;
  triggeredNodes: string[];
  latencyMs?: number;
}

interface PulseResponse {
  success: boolean;
  pulseId?: string;
  error?: string;
}

// ============================================================
// Device Authentication Middleware
// ============================================================

async function authenticateDevice(deviceId: string, deviceSecret: string) {
  const { data: device, error } = await supabase
    .from('devices')
    .select('*')
    .eq('id', deviceId)
    .eq('device_secret', deviceSecret)
    .single();
  
  if (error || !device) {
    return null;
  }
  
  return device;
}

// ============================================================
// POST /api/neuron/sync/delta
// Sync incremental changes since last sync
// ============================================================

export async function POST_deltaSync(req: Request): Promise<Response> {
  try {
    const body: DeltaSyncRequest = await req.json();
    const { deviceId, deviceSecret, sinceVersion, nodes, links } = body;
    
    // Authenticate device
    const device = await authenticateDevice(deviceId, deviceSecret);
    if (!device) {
      return Response.json(
        { success: false, error: 'Unauthorized' },
        { status: 401 }
      );
    }
    
    const tenantId = device.tenant_id;
    
    // 1. Process node deltas from device
    for (const nodeDelta of nodes) {
      if (nodeDelta.operation === 'delete') {
        await supabase
          .from('nodes')
          .update({ 
            is_deleted: true, 
            version: nodeDelta.version + 1,
            updated_at: new Date().toISOString()
          })
          .eq('id', nodeDelta.id)
          .eq('tenant_id', tenantId);
      } else {
        // Upsert node
        const { error } = await supabase
          .from('nodes')
          .upsert({
            id: nodeDelta.id,
            tenant_id: tenantId,
            type: nodeDelta.type,
            name: nodeDelta.name,
            content: nodeDelta.content,
            pos_x: nodeDelta.pos_x,
            pos_y: nodeDelta.pos_y,
            pos_z: nodeDelta.pos_z,
            is_active: nodeDelta.is_active,
            is_pinned: nodeDelta.is_pinned,
            is_favorite: nodeDelta.is_favorite,
            version: nodeDelta.version + 1,
            updated_at: new Date().toISOString(),
            source_device: deviceId
          }, {
            onConflict: 'tenant_id,type,name'
          });
        
        if (error) {
          console.error('Node upsert error:', error);
        }
      }
    }
    
    // 2. Process link deltas from device
    for (const linkDelta of links) {
      if (linkDelta.operation === 'delete') {
        await supabase
          .from('links')
          .update({ 
            is_deleted: true,
            version: linkDelta.version + 1
          })
          .eq('id', linkDelta.id)
          .eq('tenant_id', tenantId);
      } else {
        await supabase
          .from('links')
          .upsert({
            id: linkDelta.id,
            tenant_id: tenantId,
            source_node_id: linkDelta.source_node_id,
            target_node_id: linkDelta.target_node_id,
            type: linkDelta.type,
            weight: linkDelta.weight,
            confidence: linkDelta.confidence,
            is_active: linkDelta.is_active,
            version: linkDelta.version + 1,
            source_device: deviceId
          }, {
            onConflict: 'tenant_id,source_node_id,target_node_id,type'
          });
      }
    }
    
    // 3. Get server changes since device version
    const sinceDate = new Date(Date.now() - 7 * 24 * 60 * 60 * 1000); // Max 7 days
    
    const { data: serverNodes } = await supabase
      .from('nodes')
      .select('id, type, subtype, name, description, content, pos_x, pos_y, pos_z, is_active, is_pinned, is_favorite, version, updated_at')
      .eq('tenant_id', tenantId)
      .eq('is_deleted', false)
      .gt('version', sinceVersion)
      .gte('updated_at', sinceDate.toISOString());
    
    const { data: serverLinks } = await supabase
      .from('links')
      .select('id, source_node_id, target_node_id, type, weight, confidence, is_active, version, updated_at')
      .eq('tenant_id', tenantId)
      .eq('is_deleted', false)
      .gt('version', sinceVersion);
    
    // 4. Get deleted records
    const { data: deletedNodes } = await supabase
      .from('nodes')
      .select('id')
      .eq('tenant_id', tenantId)
      .eq('is_deleted', true)
      .gt('version', sinceVersion);
    
    const { data: deletedLinks } = await supabase
      .from('links')
      .select('id')
      .eq('tenant_id', tenantId)
      .eq('is_deleted', true)
      .gt('version', sinceVersion);
    
    // 5. Update device sync state
    const serverVersion = Math.max(
      ...(serverNodes?.map(n => n.version) || [0]),
      ...(serverLinks?.map(l => l.version) || [0])
    );
    
    await supabase
      .from('devices')
      .update({
        last_sync_at: new Date().toISOString(),
        sqlite_version: serverVersion,
        is_online: true,
        last_seen_at: new Date().toISOString()
      })
      .eq('id', deviceId);
    
    // 6. Record sync in log
    await supabase
      .from('sync_log')
      .insert({
        device_id: deviceId,
        sync_type: 'delta',
        direction: 'both',
        nodes_sent: nodes.length,
        nodes_received: serverNodes?.length || 0,
        links_sent: links.length,
        links_received: serverLinks?.length || 0,
        status: 'success'
      });
    
    return Response.json({
      success: true,
      serverVersion,
      nodes: serverNodes?.map(n => ({
        ...n,
        operation: 'upsert' as const
      })) || [],
      links: serverLinks?.map(l => ({
        ...l,
        operation: 'upsert' as const
      })) || [],
      deletedNodeIds: deletedNodes?.map(n => n.id) || [],
      deletedLinkIds: deletedLinks?.map(l => l.id) || []
    } satisfies DeltaSyncResponse);
    
  } catch (error) {
    console.error('Delta sync error:', error);
    return Response.json(
      { success: false, error: 'Internal server error' },
      { status: 500 }
    );
  }
}

// ============================================================
// POST /api/neuron/sync/full
// Full graph sync for device bootstrap
// ============================================================

export async function POST_fullSync(req: Request): Promise<Response> {
  try {
    const body: FullSyncRequest = await req.json();
    const { deviceId, deviceSecret, deviceType, firmwareVersion } = body;
    
    // Authenticate device
    const device = await authenticateDevice(deviceId, deviceSecret);
    if (!device) {
      return Response.json(
        { success: false, error: 'Unauthorized' },
        { status: 401 }
      );
    }
    
    const tenantId = device.tenant_id;
    
    // 1. Update device info
    await supabase
      .from('devices')
      .update({
        firmware_version: firmwareVersion,
        device_type: deviceType,
        last_seen_at: new Date().toISOString(),
        is_online: true
      })
      .eq('id', deviceId);
    
    // 2. Create a new session for this sync
    const { data: session } = await supabase
      .from('sessions')
      .insert({
        tenant_id: tenantId,
        device_id: deviceId,
        session_type: 'mqtt',
        title: `Device Sync - ${new Date().toISOString()}`,
        context_snapshot: {
          sync_type: 'full',
          device_type: deviceType,
          firmware_version: firmwareVersion
        }
      })
      .select()
      .single();
    
    // 3. Get all active nodes (paginated for large graphs)
    const allNodes: NodeDelta[] = [];
    let page = 0;
    const pageSize = 500;
    
    while (true) {
      const { data: nodes } = await supabase
        .from('nodes')
        .select('id, type, subtype, name, description, content, pos_x, pos_y, pos_z, is_active, is_pinned, is_favorite, version, updated_at')
        .eq('tenant_id', tenantId)
        .eq('is_active', true)
        .range(page * pageSize, (page + 1) * pageSize - 1);
      
      if (!nodes || nodes.length === 0) break;
      
      allNodes.push(...nodes.map(n => ({
        ...n,
        operation: 'upsert' as const
      })));
      
      if (nodes.length < pageSize) break;
      page++;
    }
    
    // 4. Get all active links
    const { data: links } = await supabase
      .from('links')
      .select('id, source_node_id, target_node_id, type, weight, confidence, is_active, version, updated_at')
      .eq('tenant_id', tenantId)
      .eq('is_active', true);
    
    // 5. Get max version
    const serverVersion = Math.max(
      ...allNodes.map(n => n.version),
      ...(links?.map(l => l.version) || [0])
    );
    
    // 6. Record sync
    await supabase
      .from('sync_log')
      .insert({
        device_id: deviceId,
        sync_type: 'full',
        direction: 'download',
        nodes_sent: 0,
        nodes_received: allNodes.length,
        links_sent: 0,
        links_received: links?.length || 0,
        status: 'success'
      });
    
    return Response.json({
      success: true,
      serverVersion,
      nodes: allNodes,
      links: links?.map(l => ({
        ...l,
        operation: 'upsert' as const
      })) || [],
      sessionId: session?.id
    } satisfies FullSyncResponse);
    
  } catch (error) {
    console.error('Full sync error:', error);
    return Response.json(
      { success: false, error: 'Internal server error' },
      { status: 500 }
    );
  }
}

// ============================================================
// POST /api/neuron/pulse
// Record real-time inference pulse
// ============================================================

export async function POST_pulse(req: Request): Promise<Response> {
  try {
    const body: PulseRequest = await req.json();
    const { deviceId, sessionId, pulseType, content, triggeredNodes, latencyMs } = body;
    
    // Authenticate device
    const device = await authenticateDevice(deviceId, req.headers.get('x-device-secret') || '');
    if (!device) {
      return Response.json(
        { success: false, error: 'Unauthorized' },
        { status: 401 }
      );
    }
    
    const tenantId = device.tenant_id;
    
    // Record pulse using the database function
    const { data: pulseId, error } = await supabase
      .rpc('record_pulse', {
        p_tenant_id: tenantId,
        p_device_id: deviceId,
        p_session_id: sessionId || null,
        p_pulse_type: pulseType,
        p_content: content,
        p_triggered_nodes: triggeredNodes,
        p_latency_ms: latencyMs || null
      });
    
    if (error) {
      console.error('Pulse recording error:', error);
      return Response.json(
        { success: false, error: 'Failed to record pulse' },
        { status: 500 }
      );
    }
    
    return Response.json({
      success: true,
      pulseId
    } satisfies PulseResponse);
    
  } catch (error) {
    console.error('Pulse error:', error);
    return Response.json(
      { success: false, error: 'Internal server error' },
      { status: 500 }
    );
  }
}

// ============================================================
// POST /api/neuron/pair
// Pair new ESP32 device with 6-digit code
// ============================================================

interface PairRequest {
  pairCode: string;
  deviceType: string;
  macAddress: string;
  deviceName: string;
  firmwareVersion: string;
}

interface PairResponse {
  success: boolean;
  deviceId?: string;
  deviceSecret?: string;
  tenantId?: string;
  error?: string;
}

export async function POST_pair(req: Request): Promise<Response> {
  try {
    const body: PairRequest = await req.json();
    const { pairCode, deviceType, macAddress, deviceName, firmwareVersion } = body;
    
    // Find pending device with matching pair code
    const { data: device, error } = await supabase
      .from('devices')
      .select('*')
      .eq('pair_code', pairCode)
      .eq('is_paired', false)
      .gt('pair_expires_at', new Date().toISOString())
      .single();
    
    if (error || !device) {
      return Response.json({
        success: false,
        error: 'Invalid or expired pair code'
      } satisfies PairResponse, { status: 400 });
    }
    
    // Generate device secret
    const deviceSecret = Array.from({ length: 64 }, () =>
      Math.random().toString(36).charAt(2)
    ).join('');
    
    // Update device with pairing info
    const { error: updateError } = await supabase
      .from('devices')
      .update({
        is_paired: true,
        device_secret: deviceSecret,
        device_type: deviceType,
        mac_address: macAddress,
        device_name: deviceName,
        firmware_version: firmwareVersion,
        pair_code: null,
        pair_expires_at: null,
        last_seen_at: new Date().toISOString(),
        is_online: true
      })
      .eq('id', device.id);
    
    if (updateError) {
      return Response.json({
        success: false,
        error: 'Failed to complete pairing'
      } satisfies PairResponse, { status: 500 });
    }
    
    return Response.json({
      success: true,
      deviceId: device.id,
      deviceSecret,
      tenantId: device.tenant_id
    } satisfies PairResponse);
    
  } catch (error) {
    console.error('Pairing error:', error);
    return Response.json(
      { success: false, error: 'Internal server error' },
      { status: 500 }
    );
  }
}

// ============================================================
// POST /api/neuron/devices
// Register new device (generates pair code)
// ============================================================

interface RegisterDeviceRequest {
  tenantId: string;
  deviceType: string;
  deviceName: string;
}

interface RegisterDeviceResponse {
  success: boolean;
  deviceId?: string;
  pairCode?: string;
  pairExpiresAt?: string;
  error?: string;
}

export async function POST_registerDevice(req: Request): Promise<Response> {
  try {
    const body: RegisterDeviceRequest = await req.json();
    const { tenantId, deviceType, deviceName } = body;
    
    // Verify tenant exists
    const { data: tenant } = await supabase
      .from('tenants')
      .select('id, max_devices')
      .eq('id', tenantId)
      .single();
    
    if (!tenant) {
      return Response.json({
        success: false,
        error: 'Invalid tenant'
      } satisfies RegisterDeviceResponse, { status: 400 });
    }
    
    // Check device limit
    const { count } = await supabase
      .from('devices')
      .select('*', { count: 'exact', head: true })
      .eq('tenant_id', tenantId)
      .eq('is_paired', true);
    
    if (count !== null && count >= tenant.max_devices) {
      return Response.json({
        success: false,
        error: `Device limit reached (${tenant.max_devices})`
      } satisfies RegisterDeviceResponse, { status: 400 });
    }
    
    // Generate 6-digit pair code
    const pairCode = Math.floor(100000 + Math.random() * 900000).toString();
    
    // Create pending device
    const { data: device, error } = await supabase
      .from('devices')
      .insert({
        tenant_id: tenantId,
        device_type: deviceType,
        device_name: deviceName,
        pair_code: pairCode,
        pair_expires_at: new Date(Date.now() + 10 * 60 * 1000).toISOString(), // 10 min
        is_paired: false
      })
      .select()
      .single();
    
    if (error) {
      return Response.json({
        success: false,
        error: 'Failed to register device'
      } satisfies RegisterDeviceResponse, { status: 500 });
    }
    
    return Response.json({
      success: true,
      deviceId: device.id,
      pairCode,
      pairExpiresAt: device.pair_expires_at
    } satisfies RegisterDeviceResponse);
    
  } catch (error) {
    console.error('Register device error:', error);
    return Response.json(
      { success: false, error: 'Internal server error' },
      { status: 500 }
    );
  }
}
