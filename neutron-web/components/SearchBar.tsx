'use client';

/**
 * SearchBar Component
 * 
 * Search nodes by name with autocomplete dropdown
 */

import React, { useState, useRef, useEffect } from 'react';
import { GraphNode } from '@/lib/graph';

interface SearchBarProps {
  value: string;
  onChange: (value: string) => void;
  results: GraphNode[];
  onSelectResult: (node: GraphNode) => void;
}

export default function SearchBar({ value, onChange, results, onSelectResult }: SearchBarProps) {
  const [isOpen, setIsOpen] = useState(false);
  const [selectedIndex, setSelectedIndex] = useState(0);
  const inputRef = useRef<HTMLInputElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  
  // Close dropdown when clicking outside
  useEffect(() => {
    function handleClickOutside(event: MouseEvent) {
      if (containerRef.current && !containerRef.current.contains(event.target as Node)) {
        setIsOpen(false);
      }
    }
    
    document.addEventListener('mousedown', handleClickOutside);
    return () => document.removeEventListener('mousedown', handleClickOutside);
  }, []);
  
  // Update selected index when results change
  useEffect(() => {
    setSelectedIndex(0);
  }, [results]);
  
  // Handle keyboard navigation
  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (!isOpen || results.length === 0) return;
    
    switch (e.key) {
      case 'ArrowDown':
        e.preventDefault();
        setSelectedIndex(prev => Math.min(prev + 1, results.length - 1));
        break;
      case 'ArrowUp':
        e.preventDefault();
        setSelectedIndex(prev => Math.max(prev - 1, 0));
        break;
      case 'Enter':
        e.preventDefault();
        if (results[selectedIndex]) {
          onSelectResult(results[selectedIndex]);
          setIsOpen(false);
          onChange('');
        }
        break;
      case 'Escape':
        setIsOpen(false);
        break;
    }
  };
  
  return (
    <div ref={containerRef} className="relative w-96">
      {/* Input */}
      <div className="relative">
        <svg
          className="absolute left-3 top-1/2 -translate-y-1/2 w-5 h-5 text-gray-400"
          fill="none"
          stroke="currentColor"
          viewBox="0 0 24 24"
        >
          <path
            strokeLinecap="round"
            strokeLinejoin="round"
            strokeWidth={2}
            d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z"
          />
        </svg>
        <input
          ref={inputRef}
          type="text"
          value={value}
          onChange={(e) => {
            onChange(e.target.value);
            setIsOpen(true);
          }}
          onFocus={() => setIsOpen(true)}
          onKeyDown={handleKeyDown}
          placeholder="Search nodes..."
          className="w-full pl-10 pr-4 py-2 bg-gray-800 border border-gray-700 rounded-lg text-white placeholder-gray-400 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-transparent"
        />
        {value && (
          <button
            onClick={() => {
              onChange('');
              inputRef.current?.focus();
            }}
            className="absolute right-3 top-1/2 -translate-y-1/2 text-gray-400 hover:text-white"
          >
            <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        )}
      </div>
      
      {/* Results dropdown */}
      {isOpen && value && results.length > 0 && (
        <div className="absolute top-full left-0 right-0 mt-2 bg-gray-800 border border-gray-700 rounded-lg shadow-xl overflow-hidden z-50">
          <div className="max-h-80 overflow-y-auto">
            {results.map((node, index) => (
              <button
                key={node.id}
                onClick={() => {
                  onSelectResult(node);
                  setIsOpen(false);
                  onChange('');
                }}
                className={`w-full flex items-center gap-3 px-4 py-3 text-left transition ${
                  index === selectedIndex
                    ? 'bg-blue-600/30'
                    : 'hover:bg-gray-700'
                }`}
              >
                <div className="w-8 h-8 rounded flex items-center justify-center text-lg bg-gray-700/50">
                  {getNodeIcon(node.type)}
                </div>
                <div className="flex-1 min-w-0">
                  <div className="text-sm font-medium text-white truncate">
                    {node.name}
                  </div>
                  <div className="text-xs text-gray-400">
                    {node.type}{node.subtype ? ` · ${node.subtype}` : ''}
                  </div>
                </div>
                <div className="text-xs text-gray-500">
                  {(node.activation_count)} activations
                </div>
              </button>
            ))}
          </div>
          
          {/* Keyboard hints */}
          <div className="px-4 py-2 bg-gray-900/50 border-t border-gray-700 text-xs text-gray-400 flex gap-4">
            <span>↑↓ Navigate</span>
            <span>↵ Select</span>
            <span>Esc Close</span>
          </div>
        </div>
      )}
      
      {/* No results */}
      {isOpen && value && results.length === 0 && (
        <div className="absolute top-full left-0 right-0 mt-2 bg-gray-800 border border-gray-700 rounded-lg shadow-xl p-4 z-50">
          <p className="text-sm text-gray-400 text-center">No nodes found</p>
        </div>
      )}
    </div>
  );
}

function getNodeIcon(type: string): string {
  const icons: Record<string, string> = {
    user: '👤', device: '📱', skill: '⚡', memory: '🧠',
    tag: '🏷️', transaction: '💸', entity: '🔮', event: '📅',
    goal: '🎯', routine: '🔄', insight: '💡', pulse: '🌊',
    webhook: '🔗', session: '💬', context: '🎭'
  };
  return icons[type] || '⬡';
}
