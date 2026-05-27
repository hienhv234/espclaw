/**
 * Neutron Dashboard - 3D Knowledge Graph Visualization
 * 
 * Built with:
 * - Next.js 14 (App Router)
 * - Supabase (Auth + Realtime)
 * - react-force-graph-3d (Three.js)
 * - TypeScript
 */

import { createClient } from '@supabase/supabase-js';

// ============================================================
// Supabase Client Configuration
// ============================================================

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseAnonKey = process.env.NEXT_PUBLIC_SUPABASE_ANON_KEY!;

// Browser client (for auth)
export const supabase = createClient(supabaseUrl, supabaseAnonKey);

// Server client (for API routes)
export const createServerClient = () => {
  return createClient(supabaseUrl, supabaseAnonKey, {
    auth: {
      autoRefreshToken: false,
      persistSession: false
    }
  });
};

// Admin client (service role - server-side only)
export const createAdminClient = () => {
  return createClient(supabaseUrl, process.env.SUPABASE_SERVICE_ROLE_KEY!, {
    auth: {
      autoRefreshToken: false,
      persistSession: false
    }
  });
};

// ============================================================
// Type Definitions
// ============================================================

export interface GraphNode {
  id: string;
  name: string;
  type: string;
  subtype?: string;
  description?: string;
  content: Record<string, any>;
  
  // 3D Position
  pos_x: number;
  pos_y: number;
  pos_z: number;
  
  // State
  is_active: boolean;
  is_pinned: boolean;
  is_favorite: boolean;
  is_focused?: boolean;
  
  // Visual properties
  color?: string;
  size?: number;
  icon?: string;
  opacity?: number;
  
  // Stats
  activation_count: number;
  pulse_strength: number;
  link_count: number;
  
  // Timestamps
  created_at: string;
  updated_at: string;
}

export interface GraphLink {
  id: string;
  source: string;
  target: string;
  type: string;
  
  // Properties
  weight: number;
  confidence: number;
  is_active: boolean;
  is_ai_generated: boolean;
  
  // Visual
  color?: string;
  opacity?: number;
  width?: number;
  animated?: boolean;
  
  // State
  last_pulsed_at?: string;
}

export interface GraphData {
  nodes: GraphNode[];
  links: GraphLink[];
  stats: GraphStats;
}

export interface GraphStats {
  total_nodes: number;
  total_links: number;
  nodes_by_type: Record<string, number>;
  links_by_type: Record<string, number>;
  active_pulses: number;
}

// ============================================================
// Domain Color Scheme
// ============================================================

export const DOMAIN_COLORS: Record<string, string> = {
  user: '#455A64',
  device: '#78909C',
  skill: '#2196F3',
  memory: '#9E9E9E',
  tag: '#FF9800',
  transaction: '#66BB6A',
  entity: '#7C4DFF',
  event: '#E91E63',
  goal: '#F44336',
  routine: '#00BCD4',
  insight: '#AB47BC',
  pulse: '#AA00FF',
  webhook: '#26A69A',
  session: '#5C6BC0',
  context: '#EC407A',
  
  // Subtypes
  finance_account: '#4CAF50',
  finance_transaction: '#8BC34A',
  finance_budget: '#009688',
  health_exercise: '#F44336',
  health_meal: '#E91E63',
  health_sleep: '#9C27B0',
  home_device: '#FF9800',
  home_room: '#FFC107',
  smart_home: '#FF5722',
  voice_input: '#03A9F4',
};

export const LINK_COLORS: Record<string, string> = {
  owns: '#4CAF50',
  part_of: '#90A4AE',
  member_of: '#B0BEC5',
  related_to: '#78909C',
  similar_to: '#B0BEC5',
  caused_by: '#FF5722',
  resulted_in: '#FF7043',
  happened_before: '#FFC107',
  happened_after: '#FFD54F',
  triggered: '#FF9800',
  followed_by: '#FFC107',
  preceded_by: '#FFD54F',
  associated_with: '#78909C',
  enabled_by: '#4CAF50',
  provides: '#66BB6A',
  input_to: '#2196F3',
  output_of: '#03A9F4',
  costs: '#F44336',
  earned_from: '#4CAF50',
  saved_by: '#009688',
  invested_in: '#00796B',
  sent_to: '#9C27B0',
  received_from: '#E91E63',
};

// ============================================================
// Helper Functions
// ============================================================

export function getNodeColor(node: GraphNode): string {
  if (node.color) return node.color;
  if (node.subtype && DOMAIN_COLORS[node.subtype]) {
    return DOMAIN_COLORS[node.subtype];
  }
  return DOMAIN_COLORS[node.type] || '#9E9E9E';
}

export function getLinkColor(link: GraphLink): string {
  if (link.color) return link.color;
  return LINK_COLORS[link.type] || '#78909C';
}

export function getNodeSize(node: GraphNode): number {
  // Base size + activation bonus
  const base = 5;
  const activationBonus = Math.log10(node.activation_count + 1) * 2;
  const pulseBonus = node.pulse_strength * 5;
  return Math.min(30, base + activationBonus + pulseBonus);
}

export function getLinkWidth(link: GraphLink): number {
  return Math.max(0.5, link.weight * 4);
}

export function getLinkOpacity(link: GraphLink): number {
  return 0.3 + link.weight * 0.5;
}

// ============================================================
// Graph Data Fetching
// ============================================================

export async function fetchGraphData(tenantId: string): Promise<GraphData> {
  const client = createServerClient();
  
  // Fetch nodes
  const { data: nodes, error: nodesError } = await client
    .from('nodes')
    .select('*')
    .eq('tenant_id', tenantId)
    .eq('is_active', true);
  
  if (nodesError) throw nodesError;
  
  // Fetch links
  const { data: links, error: linksError } = await client
    .from('links')
    .select('*')
    .eq('tenant_id', tenantId)
    .eq('is_active', true);
  
  if (linksError) throw linksError;
  
  // Fetch stats
  const { data: stats } = await client
    .from('graph_stats')
    .select('*')
    .eq('tenant_id', tenantId)
    .single();
  
  // Transform to visualization format
  const graphNodes: GraphNode[] = (nodes || []).map(node => ({
    ...node,
    content: typeof node.content === 'string' ? JSON.parse(node.content) : node.content,
    is_focused: false
  }));
  
  const graphLinks: GraphLink[] = (links || []).map(link => ({
    ...link,
    animated: link.last_pulsed_at && 
      Date.now() - new Date(link.last_pulsed_at).getTime() < 60000
  }));
  
  return {
    nodes: graphNodes,
    links: graphLinks,
    stats: stats || {
      total_nodes: graphNodes.length,
      total_links: graphLinks.length,
      nodes_by_type: {},
      links_by_type: {},
      active_pulses: 0
    }
  };
}

export async function fetchNearbyNodes(
  tenantId: string,
  nodeId: string,
  radius: number = 30
): Promise<GraphNode[]> {
  const client = createServerClient();
  
  const { data, error } = await client
    .rpc('get_nearby_nodes', {
      p_tenant_id: tenantId,
      p_node_id: nodeId,
      p_radius: radius,
      p_limit: 20
    });
  
  if (error) throw error;
  return data || [];
}

export async function fetchConnectedNodes(
  tenantId: string,
  nodeId: string,
  hops: number = 2
): Promise<{ node: GraphNode; depth: number }[]> {
  const client = createServerClient();
  
  const { data, error } = await client
    .rpc('get_connected_nodes', {
      p_tenant_id: tenantId,
      p_node_id: nodeId,
      p_hops: hops,
      p_direction: 'both'
    });
  
  if (error) throw error;
  return data || [];
}

// ============================================================
// Graph Mutations
// ============================================================

export async function createNode(
  tenantId: string,
  node: Partial<GraphNode>
): Promise<GraphNode> {
  const client = createAdminClient();
  
  const { data, error } = await client
    .from('nodes')
    .insert({
      tenant_id: tenantId,
      type: node.type,
      subtype: node.subtype,
      name: node.name,
      description: node.description,
      content: node.content || {},
      pos_x: node.pos_x ?? 50,
      pos_y: node.pos_y ?? 500,
      pos_z: node.pos_z ?? 50,
      is_active: true
    })
    .select()
    .single();
  
  if (error) throw error;
  return data;
}

export async function createLink(
  tenantId: string,
  link: {
    source_node_id: string;
    target_node_id: string;
    type: string;
    weight?: number;
  }
): Promise<GraphLink> {
  const client = createAdminClient();
  
  const { data, error } = await client
    .from('links')
    .insert({
      tenant_id: tenantId,
      source_node_id: link.source_node_id,
      target_node_id: link.target_node_id,
      type: link.type,
      weight: link.weight ?? 0.5,
      is_active: true
    })
    .select()
    .single();
  
  if (error) throw error;
  return data;
}

export async function updateNode(
  tenantId: string,
  nodeId: string,
  updates: Partial<GraphNode>
): Promise<GraphNode> {
  const client = createAdminClient();
  
  const { data, error } = await client
    .from('nodes')
    .update(updates)
    .eq('id', nodeId)
    .eq('tenant_id', tenantId)
    .select()
    .single();
  
  if (error) throw error;
  return data;
}

export async function deleteNode(
  tenantId: string,
  nodeId: string
): Promise<void> {
  const client = createAdminClient();
  
  const { error } = await client
    .from('nodes')
    .update({ is_active: false })
    .eq('id', nodeId)
    .eq('tenant_id', tenantId);
  
  if (error) throw error;
}

export async function deleteLink(
  tenantId: string,
  linkId: string
): Promise<void> {
  const client = createAdminClient();
  
  const { error } = await client
    .from('links')
    .update({ is_active: false })
    .eq('id', linkId)
    .eq('tenant_id', tenantId);
  
  if (error) throw error;
}

// ============================================================
// Semantic Search
// ============================================================

export async function searchNodes(
  tenantId: string,
  query: string
): Promise<GraphNode[]> {
  const client = createServerClient();
  
  const { data, error } = await client
    .rpc('search_nodes', {
      p_tenant_id: tenantId,
      p_query: query,
      p_limit: 20
    });
  
  if (error) throw error;
  return data || [];
}

export async function findSimilarNodes(
  tenantId: string,
  embedding: number[],
  threshold: number = 0.7
): Promise<GraphNode[]> {
  const client = createServerClient();
  
  const { data, error } = await client
    .rpc('find_similar_nodes', {
      p_tenant_id: tenantId,
      p_embedding: embedding,
      p_threshold: threshold,
      p_limit: 10
    });
  
  if (error) throw error;
  return data || [];
}
