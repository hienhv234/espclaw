'use client';

/**
 * NodeDetailPanel Component
 * 
 * Shows detailed information about a selected node and allows
 * visual logic mapping (creating connections to other nodes)
 */

import React, { useState, useMemo } from 'react';
import { GraphNode, GraphLink, getNodeColor, DOMAIN_COLORS, LINK_COLORS } from '@/lib/graph';

interface NodeDetailPanelProps {
  node: GraphNode;
  graphData: { nodes: GraphNode[]; links: GraphLink[] };
  onClose: () => void;
  onDelete: () => void;
  onCreateLink: (sourceId: string, targetId: string, linkType: string) => void;
  onNodeClick: (node: GraphNode) => void;
}

export default function NodeDetailPanel({
  node,
  graphData,
  onClose,
  onDelete,
  onCreateLink,
  onNodeClick
}: NodeDetailPanelProps) {
  const [activeTab, setActiveTab] = useState<'info' | 'links' | 'create'>('info');
  const [selectedLinkTarget, setSelectedLinkTarget] = useState<string | null>(null);
  const [selectedLinkType, setSelectedLinkType] = useState<string>('related_to');
  
  // Get connected nodes
  const connectedLinks = useMemo(() => {
    return graphData.links.filter(
      link => link.source === node.id || link.target === node.id
    );
  }, [graphData.links, node.id]);
  
  const connectedNodes = useMemo((): { link: any; node: GraphNode }[] => {
    return connectedLinks.map(link => {
      const otherId = link.source === node.id ? link.target : link.source;
      const otherNode = graphData.nodes.find(n => n.id === otherId);
      return { link, node: otherNode };
    }).filter((item): item is { link: any; node: GraphNode } => Boolean(item.node));
  }, [connectedLinks, graphData.nodes, node.id]);
  
  // Available link types
  const linkTypes = [
    { value: 'owns', label: 'Owns', color: '#4CAF50' },
    { value: 'related_to', label: 'Related To', color: '#78909C' },
    { value: 'similar_to', label: 'Similar To', color: '#B0BEC5' },
    { value: 'triggered', label: 'Triggered By', color: '#FF9800' },
    { value: 'followed_by', label: 'Followed By', color: '#FFC107' },
    { value: 'preceded_by', label: 'Preceded By', color: '#FFD54F' },
    { value: 'input_to', label: 'Input To', color: '#2196F3' },
    { value: 'output_of', label: 'Output Of', color: '#03A9F4' },
    { value: 'part_of', label: 'Part Of', color: '#90A4AE' },
    { value: 'associated_with', label: 'Associated With', color: '#78909C' },
  ];
  
  // Unconnected nodes for creating links
  const availableNodes = useMemo(() => {
    const connectedIds = new Set([node.id, ...connectedNodes.map(c => c.node!.id)]);
    return graphData.nodes.filter(n => !connectedIds.has(n.id));
  }, [graphData.nodes, node.id, connectedNodes]);
  
  const handleCreateLink = () => {
    if (selectedLinkTarget && selectedLinkType) {
      onCreateLink(node.id, selectedLinkTarget, selectedLinkType);
      setSelectedLinkTarget(null);
      setActiveTab('links');
    }
  };
  
  return (
    <div className="w-96 bg-gray-900 border-l border-gray-800 h-full overflow-hidden flex flex-col">
      {/* Header */}
      <div className="p-4 border-b border-gray-800">
        <div className="flex items-start justify-between mb-3">
          <div className="flex items-center gap-3">
            <div 
              className="w-10 h-10 rounded-lg flex items-center justify-center text-xl"
              style={{ backgroundColor: getNodeColor(node) + '33' }}
            >
              {getNodeIcon(node.type)}
            </div>
            <div>
              <h2 className="font-semibold text-white">{node.name}</h2>
              <p className="text-xs text-gray-400">
                {node.type}{node.subtype ? ` · ${node.subtype}` : ''}
              </p>
            </div>
          </div>
          <button
            onClick={onClose}
            className="p-2 hover:bg-gray-800 rounded-lg transition text-gray-400 hover:text-white"
          >
            <svg className="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>
        
        {/* Tabs */}
        <div className="flex gap-2">
          {['info', 'links', 'connect'].map(tab => (
            <button
              key={tab}
              onClick={() => setActiveTab(tab as any)}
              className={`px-3 py-1.5 text-sm rounded-lg transition ${
                activeTab === tab 
                  ? 'bg-blue-600 text-white' 
                  : 'text-gray-400 hover:text-white hover:bg-gray-800'
              }`}
            >
              {tab === 'info' ? 'Info' : tab === 'links' ? `Links (${connectedNodes.length})` : 'Connect'}
            </button>
          ))}
        </div>
      </div>
      
      {/* Content */}
      <div className="flex-1 overflow-y-auto p-4">
        {activeTab === 'info' && (
          <div className="space-y-4">
            {/* Stats */}
            <div className="grid grid-cols-3 gap-3">
              <StatCard label="Activations" value={node.activation_count} />
              <StatCard label="Links" value={node.link_count} />
              <StatCard 
                label="Pulse" 
                value={`${Math.round(node.pulse_strength * 100)}%`}
                color={node.pulse_strength > 0.7 ? '#4CAF50' : node.pulse_strength > 0.3 ? '#FFC107' : '#9E9E9E'}
              />
            </div>
            
            {/* Position */}
            <div className="bg-gray-800/50 rounded-lg p-3">
              <h3 className="text-xs font-medium text-gray-400 mb-2">3D Position</h3>
              <div className="grid grid-cols-3 gap-2 text-center">
                <div className="bg-gray-700/50 rounded p-2">
                  <div className="text-xs text-gray-400">X</div>
                  <div className="font-mono text-sm">{node.pos_x.toFixed(1)}</div>
                </div>
                <div className="bg-gray-700/50 rounded p-2">
                  <div className="text-xs text-gray-400">Y</div>
                  <div className="font-mono text-sm">{node.pos_y.toFixed(1)}</div>
                </div>
                <div className="bg-gray-700/50 rounded p-2">
                  <div className="text-xs text-gray-400">Z</div>
                  <div className="font-mono text-sm">{node.pos_z.toFixed(1)}</div>
                </div>
              </div>
            </div>
            
            {/* Description */}
            {node.description && (
              <div>
                <h3 className="text-xs font-medium text-gray-400 mb-2">Description</h3>
                <p className="text-sm text-gray-300">{node.description}</p>
              </div>
            )}
            
            {/* Content */}
            {node.content && Object.keys(node.content).length > 0 && (
              <div>
                <h3 className="text-xs font-medium text-gray-400 mb-2">Content</h3>
                <pre className="bg-gray-800 rounded-lg p-3 text-xs text-gray-300 overflow-x-auto">
                  {JSON.stringify(node.content, null, 2)}
                </pre>
              </div>
            )}
            
            {/* Timestamps */}
            <div className="text-xs text-gray-500">
              <div>Created: {new Date(node.created_at).toLocaleString()}</div>
              <div>Updated: {new Date(node.updated_at).toLocaleString()}</div>
            </div>
          </div>
        )}
        
        {activeTab === 'links' && (
          <div className="space-y-3">
            {connectedNodes.length === 0 ? (
              <p className="text-sm text-gray-400 text-center py-8">
                No connections yet. Use "Connect" to create links.
              </p>
            ) : (
              connectedNodes.map(({ link, node: connectedNode }) => (
                <button
                  key={link.id}
                  onClick={() => connectedNode && onNodeClick(connectedNode)}
                  className="w-full flex items-center gap-3 p-3 bg-gray-800/50 hover:bg-gray-800 rounded-lg transition text-left"
                >
                  <div 
                    className="w-8 h-8 rounded flex items-center justify-center text-sm"
                    style={{ backgroundColor: getNodeColor(connectedNode) + '33' }}
                  >
                    {getNodeIcon(connectedNode.type)}
                  </div>
                  <div className="flex-1 min-w-0">
                    <div className="text-sm font-medium text-white truncate">
                      {connectedNode.name}
                    </div>
                    <div className="flex items-center gap-2 mt-0.5">
                      <span 
                        className="text-xs px-1.5 py-0.5 rounded"
                        style={{ 
                          backgroundColor: LINK_COLORS[link.type] + '33',
                          color: LINK_COLORS[link.type]
                        }}
                      >
                        {link.type}
                      </span>
                      {link.is_ai_generated && (
                        <span className="text-xs text-purple-400">(AI)</span>
                      )}
                    </div>
                  </div>
                  <div className="text-xs text-gray-400">
                    {(link.weight * 100).toFixed(0)}%
                  </div>
                </button>
              ))
            )}
          </div>
        )}
        
        {activeTab === 'create' && (
          <div className="space-y-4">
            <p className="text-sm text-gray-400">
              Create a connection from this node to another node.
            </p>

            {/* Link type selection */}
            <div>
              <label className="text-xs font-medium text-gray-400 mb-2 block">Link Type</label>
              <div className="grid grid-cols-2 gap-2">
                {linkTypes.map(lt => (
                  <button
                    key={lt.value}
                    onClick={() => setSelectedLinkType(lt.value)}
                    className={`px-3 py-2 text-sm rounded-lg transition text-left ${
                      selectedLinkType === lt.value
                        ? 'ring-2 ring-blue-500'
                        : 'bg-gray-800 hover:bg-gray-700'
                    }`}
                    style={{
                      backgroundColor: selectedLinkType === lt.value
                        ? lt.color + '33'
                        : undefined
                    }}
                  >
                    <div
                      className="w-2 h-2 rounded-full inline-block mr-2"
                      style={{ backgroundColor: lt.color }}
                    />
                    {lt.label}
                  </button>
                ))}
              </div>
            </div>

            {/* Target node selection */}
            <div>
              <label className="text-xs font-medium text-gray-400 mb-2 block">
                Target Node ({availableNodes.length} available)
              </label>
              <div className="space-y-2 max-h-64 overflow-y-auto">
                {availableNodes.slice(0, 20).map(targetNode => (
                  <button
                    key={targetNode.id}
                    onClick={() => setSelectedLinkTarget(targetNode.id)}
                    className={`w-full flex items-center gap-3 p-2 rounded-lg transition text-left ${
                      selectedLinkTarget === targetNode.id
                        ? 'bg-blue-600/30 ring-2 ring-blue-500'
                        : 'bg-gray-800/50 hover:bg-gray-800'
                    }`}
                  >
                    <div
                      className="w-6 h-6 rounded flex items-center justify-center text-xs"
                      style={{ backgroundColor: getNodeColor(targetNode) + '33' }}
                    >
                      {getNodeIcon(targetNode.type)}
                    </div>
                    <div className="flex-1 min-w-0">
                      <div className="text-sm text-white truncate">{targetNode.name}</div>
                      <div className="text-xs text-gray-500">{targetNode.type}</div>
                    </div>
                  </button>
                ))}
              </div>
            </div>

            {/* Create button */}
            <button
              onClick={handleCreateLink}
              disabled={!selectedLinkTarget}
              className={`w-full py-3 rounded-lg font-medium transition ${
                selectedLinkTarget
                  ? 'bg-blue-600 hover:bg-blue-500 text-white'
                  : 'bg-gray-700 text-gray-500 cursor-not-allowed'
              }`}
            >
              Create Link
            </button>
          </div>
        )}
      </div>
      
      {/* Footer */}
      <div className="p-4 border-t border-gray-800">
        <button
          onClick={onDelete}
          className="w-full py-2 text-sm text-red-400 hover:text-red-300 hover:bg-red-900/20 rounded-lg transition"
        >
          Delete Node
        </button>
      </div>
    </div>
  );
}

// ============================================================
// Sub-components
// ============================================================

interface StatCardProps {
  label: string;
  value: number | string;
  color?: string;
}

function StatCard({ label, value, color }: StatCardProps) {
  return (
    <div className="bg-gray-800/50 rounded-lg p-3 text-center">
      <div className="text-xs text-gray-400 mb-1">{label}</div>
      <div 
        className="text-lg font-semibold"
        style={{ color: color || '#fff' }}
      >
        {value}
      </div>
    </div>
  );
}

// ============================================================
// Helper Functions
// ============================================================

function getNodeIcon(type: string): string {
  const icons: Record<string, string> = {
    user: '👤',
    device: '📱',
    skill: '⚡',
    memory: '🧠',
    tag: '🏷️',
    transaction: '💸',
    entity: '🔮',
    event: '📅',
    goal: '🎯',
    routine: '🔄',
    insight: '💡',
    pulse: '🌊',
    webhook: '🔗',
    session: '💬',
    context: '🎭'
  };
  return icons[type] || '⬡';
}
