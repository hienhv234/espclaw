'use client';

/**
 * ControlPanel Component
 * 
 * Bottom control bar with actions for creating nodes
 * and graph controls
 */

import React, { useState } from 'react';
import { GraphNode } from '@/lib/graph';

interface ControlPanelProps {
  onCreateNode: (nodeData: Partial<GraphNode>) => Promise<GraphNode>;
  onZoomIn: () => void;
  onZoomOut: () => void;
  onResetView: () => void;
}

export default function ControlPanel({ onCreateNode, onZoomIn, onZoomOut, onResetView }: ControlPanelProps) {
  const [showCreateModal, setShowCreateModal] = useState(false);
  const [newNode, setNewNode] = useState({
    name: '',
    type: 'entity',
    description: '',
    pos_y: 500
  });
  
  const nodeTypes = [
    { value: 'skill', label: 'Skill', icon: '⚡', color: '#2196F3' },
    { value: 'memory', label: 'Memory', icon: '🧠', color: '#9E9E9E' },
    { value: 'tag', label: 'Tag', icon: '🏷️', color: '#FF9800' },
    { value: 'transaction', label: 'Transaction', icon: '💸', color: '#66BB6A' },
    { value: 'event', label: 'Event', icon: '📅', color: '#E91E63' },
    { value: 'entity', label: 'Entity', icon: '🔮', color: '#7C4DFF' },
    { value: 'routine', label: 'Routine', icon: '🔄', color: '#00BCD4' },
    { value: 'goal', label: 'Goal', icon: '🎯', color: '#F44336' },
  ];
  
  const handleCreate = async () => {
    if (!newNode.name.trim()) return;
    
    try {
      await onCreateNode({
        name: newNode.name,
        type: newNode.type as GraphNode['type'],
        description: newNode.description || undefined,
        pos_x: 50 + Math.random() * 20,
        pos_y: newNode.pos_y,
        pos_z: 50 + Math.random() * 20,
        content: {}
      });
      
      setNewNode({ name: '', type: 'entity', description: '', pos_y: 500 });
      setShowCreateModal(false);
    } catch (err) {
      console.error('Failed to create node:', err);
    }
  };
  
  return (
    <>
      {/* Control Bar */}
      <div className="fixed bottom-6 left-1/2 -translate-x-1/2 z-40">
        <div className="flex items-center gap-2 bg-gray-900/90 backdrop-blur-md border border-gray-700 rounded-full px-4 py-2 shadow-2xl">
          {/* Zoom controls */}
          <button
            onClick={onZoomIn}
            className="p-2 hover:bg-gray-800 rounded-full transition text-gray-400 hover:text-white"
            title="Zoom In"
          >
            <svg className="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0zM10 7v3m0 0v3m0-3h3m-3 0H7" />
            </svg>
          </button>
          
          <button
            onClick={onZoomOut}
            className="p-2 hover:bg-gray-800 rounded-full transition text-gray-400 hover:text-white"
            title="Zoom Out"
          >
            <svg className="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0zM13 10H7" />
            </svg>
          </button>
          
          <div className="w-px h-6 bg-gray-700 mx-2" />
          
          {/* Reset view */}
          <button
            onClick={onResetView}
            className="p-2 hover:bg-gray-800 rounded-full transition text-gray-400 hover:text-white"
            title="Reset View"
          >
            <svg className="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
            </svg>
          </button>
          
          <div className="w-px h-6 bg-gray-700 mx-2" />
          
          {/* Create node */}
          <button
            onClick={() => setShowCreateModal(true)}
            className="flex items-center gap-2 px-4 py-2 bg-blue-600 hover:bg-blue-500 rounded-full transition text-white font-medium"
          >
            <svg className="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M12 4v16m8-8H4" />
            </svg>
            <span>Add Node</span>
          </button>
        </div>
      </div>
      
      {/* Create Node Modal */}
      {showCreateModal && (
        <div className="fixed inset-0 z-50 flex items-center justify-center">
          {/* Backdrop */}
          <div 
            className="absolute inset-0 bg-black/60 backdrop-blur-sm"
            onClick={() => setShowCreateModal(false)}
          />
          
          {/* Modal */}
          <div className="relative bg-gray-900 border border-gray-700 rounded-2xl w-full max-w-md shadow-2xl">
            {/* Header */}
            <div className="flex items-center justify-between p-6 border-b border-gray-800">
              <h2 className="text-xl font-semibold text-white">Create New Node</h2>
              <button
                onClick={() => setShowCreateModal(false)}
                className="p-2 hover:bg-gray-800 rounded-lg transition text-gray-400 hover:text-white"
              >
                <svg className="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M6 18L18 6M6 6l12 12" />
                </svg>
              </button>
            </div>
            
            {/* Content */}
            <div className="p-6 space-y-5">
              {/* Name */}
              <div>
                <label className="block text-sm font-medium text-gray-300 mb-2">
                  Name
                </label>
                <input
                  type="text"
                  value={newNode.name}
                  onChange={(e) => setNewNode(prev => ({ ...prev, name: e.target.value }))}
                  placeholder="Enter node name..."
                  className="w-full px-4 py-3 bg-gray-800 border border-gray-700 rounded-lg text-white placeholder-gray-500 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-transparent"
                  autoFocus
                />
              </div>
              
              {/* Type */}
              <div>
                <label className="block text-sm font-medium text-gray-300 mb-2">
                  Type
                </label>
                <div className="grid grid-cols-4 gap-2">
                  {nodeTypes.map(type => (
                    <button
                      key={type.value}
                      onClick={() => setNewNode(prev => ({ ...prev, type: type.value }))}
                      className={`flex flex-col items-center gap-1 p-3 rounded-lg transition ${
                        newNode.type === type.value
                          ? 'bg-blue-600/30 ring-2 ring-blue-500'
                          : 'bg-gray-800 hover:bg-gray-700'
                      }`}
                    >
                      <span className="text-xl">{type.icon}</span>
                      <span className="text-xs text-gray-300">{type.label}</span>
                    </button>
                  ))}
                </div>
              </div>
              
              {/* Domain Layer (Y Position) */}
              <div>
                <label className="block text-sm font-medium text-gray-300 mb-2">
                  Domain Layer
                </label>
                <select
                  value={newNode.pos_y}
                  onChange={(e) => setNewNode(prev => ({ ...prev, pos_y: Number(e.target.value) }))}
                  className="w-full px-4 py-3 bg-gray-800 border border-gray-700 rounded-lg text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
                >
                  <option value={300}>Finance (300-399)</option>
                  <option value={400}>Health (400-499)</option>
                  <option value={500}>Home (500-599)</option>
                  <option value={600}>Social (600-699)</option>
                  <option value={700}>Knowledge (700-799)</option>
                  <option value={800}>Creative (800-899)</option>
                  <option value={500}>General (500)</option>
                </select>
              </div>
              
              {/* Description */}
              <div>
                <label className="block text-sm font-medium text-gray-300 mb-2">
                  Description (optional)
                </label>
                <textarea
                  value={newNode.description}
                  onChange={(e) => setNewNode(prev => ({ ...prev, description: e.target.value }))}
                  placeholder="Add a description..."
                  rows={3}
                  className="w-full px-4 py-3 bg-gray-800 border border-gray-700 rounded-lg text-white placeholder-gray-500 focus:outline-none focus:ring-2 focus:ring-blue-500 resize-none"
                />
              </div>
            </div>
            
            {/* Footer */}
            <div className="flex gap-3 p-6 border-t border-gray-800">
              <button
                onClick={() => setShowCreateModal(false)}
                className="flex-1 py-3 bg-gray-800 hover:bg-gray-700 rounded-lg font-medium transition text-gray-300"
              >
                Cancel
              </button>
              <button
                onClick={handleCreate}
                disabled={!newNode.name.trim()}
                className={`flex-1 py-3 rounded-lg font-medium transition ${
                  newNode.name.trim()
                    ? 'bg-blue-600 hover:bg-blue-500 text-white'
                    : 'bg-gray-700 text-gray-500 cursor-not-allowed'
                }`}
              >
                Create
              </button>
            </div>
          </div>
        </div>
      )}
    </>
  );
}
