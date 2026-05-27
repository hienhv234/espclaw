'use client';

/**
 * KnowledgeGraph3D Component
 * 
 * 3D Knowledge Graph visualization using react-force-graph-3d
 * Features:
 * - Real-time particle animation on active links
 * - Spatial positioning based on domain layers
 * - Interactive node selection and exploration
 * - Pulse strength visualization
 */

import React, { useCallback, useEffect, useRef, useState } from 'react';
import dynamic from 'next/dynamic';
import { GraphData, GraphNode, GraphLink, getNodeColor, getNodeSize, getLinkColor, getLinkWidth, getLinkOpacity } from '@/lib/graph';

// Dynamically import to avoid SSR issues with Three.js
const ForceGraph3D = dynamic(() => import('react-force-graph-3d'), { 
  ssr: false,
  loading: () => (
    <div className="flex items-center justify-center h-full bg-gray-900">
      <div className="text-center">
        <div className="w-16 h-16 border-4 border-blue-500 border-t-transparent rounded-full animate-spin mx-auto mb-4" />
        <p className="text-gray-400">Loading 3D Graph...</p>
      </div>
    </div>
  )
});

// Particle system for link animation
interface Particle {
  id: string;
  linkId: string;
  progress: number;
  speed: number;
}

interface KnowledgeGraph3DProps {
  graphData: GraphData;
  onNodeClick?: (node: GraphNode) => void;
  onNodeHover?: (node: GraphNode | null) => void;
  onLinkClick?: (link: GraphLink) => void;
  highlightedNodeId?: string;
  className?: string;
}

export default function KnowledgeGraph3D({
  graphData,
  onNodeClick,
  onNodeHover,
  onLinkClick,
  highlightedNodeId,
  className = ''
}: KnowledgeGraph3DProps) {
  const graphRef = useRef<any>(null);
  const [particles, setParticles] = useState<Particle[]>([]);
  const [dimensions, setDimensions] = useState({ width: 800, height: 600 });
  
  // Resize handler
  useEffect(() => {
    const updateDimensions = () => {
      const container = document.getElementById('graph-container');
      if (container) {
        setDimensions({
          width: container.clientWidth,
          height: container.clientHeight
        });
      }
    };
    
    updateDimensions();
    window.addEventListener('resize', updateDimensions);
    return () => window.removeEventListener('resize', updateDimensions);
  }, []);
  
  // Particle animation loop
  useEffect(() => {
    const animatedLinks = graphData.links.filter(l => l.animated);
    
    // Initialize particles for animated links
    const newParticles: Particle[] = animatedLinks.flatMap(link => 
      Array.from({ length: 3 }, (_, i) => ({
        id: `${link.id}-p${i}`,
        linkId: link.id,
        progress: i / 3,
        speed: 0.5 + Math.random() * 0.5
      }))
    );
    
    setParticles(newParticles);
    
    // Animation loop
    let animationId: number;
    const animate = () => {
      setParticles(prev => prev.map(p => {
        const newProgress = p.progress + p.speed * 0.02;
        return {
          ...p,
          progress: newProgress > 1 ? 0 : newProgress
        };
      }));
      animationId = requestAnimationFrame(animate);
    };
    
    animationId = requestAnimationFrame(animate);
    
    return () => cancelAnimationFrame(animationId);
  }, [graphData.links]);
  
  // Center on highlighted node
  useEffect(() => {
    if (highlightedNodeId && graphRef.current) {
      const node = graphData.nodes.find(n => n.id === highlightedNodeId);
      if (node) {
        graphRef.current.centerAt(node.pos_x, node.pos_y, 1000);
        graphRef.current.zoom(2, 1000);
      }
    }
  }, [highlightedNodeId, graphData.nodes]);
  
  // Custom node rendering
  const nodeThreeObject = useCallback((node: any) => {
    const size = getNodeSize(node as GraphNode);
    const color = getNodeColor(node as GraphNode);
    const isHighlighted = node.id === highlightedNodeId;
    
    // Create sprite with glow effect
    const sprite = new (window as any).THREE.Sprite(
      new (window as any).THREE.SpriteMaterial({
        color: color,
        transparent: true,
        opacity: 0.8 + (node.pulse_strength || 0) * 0.2,
        blending: (window as any).THREE.AdditiveBlending
      })
    );
    
    sprite.scale.set(size * 2, size * 2, 1);
    
    // Add highlight ring if selected
    if (isHighlighted) {
      const ring = new (window as any).THREE.RingGeometry(size * 1.5, size * 2, 32);
      const ringMat = new (window as any).THREE.MeshBasicMaterial({
        color: '#FFFFFF',
        transparent: true,
        opacity: 0.8,
        side: (window as any).THREE.DoubleSide
      });
      const ringMesh = new (window as any).THREE.Mesh(ring, ringMat);
      ringMesh.rotation.x = Math.PI / 2;
      sprite.add(ringMesh);
    }
    
    return sprite;
  }, [highlightedNodeId]);
  
  // Custom link rendering
  const linkThreeObject = useCallback((link: any) => {
    const source = link.source as GraphNode;
    const target = link.target as GraphNode;
    
    if (!source || !target || typeof source === 'string' || typeof target === 'string') {
      return null;
    }
    
    const color = getLinkColor(link as GraphLink);
    const opacity = getLinkOpacity(link as GraphLink);
    const width = getLinkWidth(link as GraphLink);
    
    // Calculate control point for curved lines
    const midX = (source.pos_x + target.pos_x) / 2;
    const midY = (source.pos_y + target.pos_y) / 2;
    const midZ = (source.pos_z + target.pos_z) / 2;
    
    // Add curve perpendicular to the line
    const dx = target.pos_x - source.pos_x;
    const dy = target.pos_y - source.pos_y;
    const dz = target.pos_z - source.pos_z;
    const curveOffset = link.curve_strength || 0;
    
    const curve = new (window as any).THREE.QuadraticBezierCurve3(
      new (window as any).THREE.Vector3(source.pos_x, source.pos_y, source.pos_z),
      new (window as any).THREE.Vector3(
        midX + (dz * curveOffset),
        midY + (-dx * curveOffset),
        midZ + (dy * curveOffset)
      ),
      new (window as any).THREE.Vector3(target.pos_x, target.pos_y, target.pos_z)
    );
    
    const points = curve.getPoints(50);
    const geometry = new (window as any).THREE.BufferGeometry().setFromPoints(points);
    
    const material = new (window as any).THREE.LineBasicMaterial({
      color: color,
      transparent: true,
      opacity: opacity,
      linewidth: width
    });
    
    return new (window as any).THREE.Line(geometry, material);
  }, []);
  
  // Transform data for react-force-graph
  const transformedData = {
    nodes: graphData.nodes.map(node => ({
      ...node,
      x: node.pos_x * 10 - 500,  // Scale and center
      y: node.pos_y - 500,
      z: node.pos_z * 10 - 500
    })),
    links: graphData.links.map(link => ({
      ...link,
      source: link.source,
      target: link.target
    }))
  };
  
  return (
    <div id="graph-container" className={`relative ${className}`}>
      <ForceGraph3D
        ref={graphRef}
        graphData={transformedData}
        width={dimensions.width}
        height={dimensions.height}
        backgroundColor="#0a0a0f"
        
        // Node settings
        nodeLabel={(node: any) => `
          <div class="bg-gray-800 px-3 py-2 rounded-lg border border-gray-700 shadow-xl">
            <div class="flex items-center gap-2 mb-1">
              <span class="text-2xl">${getNodeIcon(node.type)}</span>
              <span class="font-semibold text-white">${node.name}</span>
            </div>
            <div class="text-xs text-gray-400">
              ${node.type}${node.subtype ? ` · ${node.subtype}` : ''}
            </div>
            ${node.description ? `<div class="text-xs text-gray-300 mt-1">${node.description.slice(0, 100)}</div>` : ''}
            <div class="flex gap-3 mt-2 text-xs">
              <span class="text-blue-400">Activations: ${node.activation_count}</span>
              <span class="text-purple-400">Links: ${node.link_count}</span>
            </div>
          </div>
        `}
        nodeThreeObject={nodeThreeObject}
        nodeThreeObjectExtend={false}
        
        // Link settings
        linkLabel={(link: any) => `
          <div class="bg-gray-800 px-2 py-1 rounded text-xs">
            <span class="text-gray-300">${link.type}</span>
            ${link.is_ai_generated ? '<span class="ml-1 text-xs text-purple-400">(AI)</span>' : ''}
          </div>
        `}
        linkThreeObject={linkThreeObject}
        linkDirectionalParticles={0}  // We use custom particle system
        
        // Interaction
        onNodeClick={(node: any) => onNodeClick?.(node)}
        onNodeHover={(node: any) => onNodeHover?.(node)}
        onLinkClick={(link: any) => onLinkClick?.(link)}
        
        // Physics
        d3VelocityDecay={0.3}
        warmupTicks={100}
        cooldownTicks={1000}
        
        // Controls
        enableNodeDrag={true}
        enableNavigationControls={true}
        showNavInfo={true}
      />
      
      {/* Particle overlay */}
      <ParticleOverlay 
        particles={particles} 
        graphData={graphData}
        dimensions={dimensions}
      />
      
      {/* Stats overlay */}
      <GraphStatsOverlay stats={graphData.stats} />
    </div>
  );
}

// ============================================================
// Particle Overlay Component
// ============================================================

interface ParticleOverlayProps {
  particles: Particle[];
  graphData: any;
  dimensions: { width: number; height: number };
}

function ParticleOverlay({ particles, graphData, dimensions }: ParticleOverlayProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    
    // Clear canvas
    ctx.clearRect(0, 0, dimensions.width, dimensions.height);
    
    // Draw particles
    particles.forEach(particle => {
      const link = graphData.links.find((l: any) => l.id === particle.linkId);
      if (!link) return;

      const sourceId = typeof link.source === 'object' ? (link.source as any).id : link.source;
      const targetId = typeof link.target === 'object' ? (link.target as any).id : link.target;
      const sourceNode = graphData.nodes.find((n: any) => n.id === sourceId);
      const targetNode = graphData.nodes.find((n: any) => n.id === targetId);
      
      if (!sourceNode || !targetNode) return;
      
      // Project 3D to 2D (simplified)
      const projectPoint = (node: any) => ({
        x: dimensions.width / 2 + (node.pos_x * 10 - 500),
        y: dimensions.height / 2 + (node.pos_y - 500)
      });
      
      const p1 = projectPoint(sourceNode);
      const p2 = projectPoint(targetNode);
      
      // Bezier curve position
      const t = particle.progress;
      const x = (1 - t) * (1 - t) * p1.x + 2 * (1 - t) * t * ((p1.x + p2.x) / 2) + t * t * p2.x;
      const y = (1 - t) * (1 - t) * p1.y + 2 * (1 - t) * t * ((p1.y + p2.y) / 2) + t * t * p2.y;
      
      // Draw glow
      const gradient = ctx.createRadialGradient(x, y, 0, x, y, 8);
      gradient.addColorStop(0, getLinkColor(link));
      gradient.addColorStop(1, 'transparent');
      
      ctx.globalAlpha = 0.8;
      ctx.fillStyle = gradient;
      ctx.beginPath();
      ctx.arc(x, y, 8, 0, Math.PI * 2);
      ctx.fill();
    });
    
  }, [particles, graphData, dimensions]);
  
  return (
    <canvas
      ref={canvasRef}
      width={dimensions.width}
      height={dimensions.height}
      className="absolute top-0 left-0 pointer-events-none"
      style={{ mixBlendMode: 'screen' }}
    />
  );
}

// ============================================================
// Stats Overlay Component
// ============================================================

interface GraphStatsOverlayProps {
  stats: GraphData['stats'];
}

function GraphStatsOverlay({ stats }: GraphStatsOverlayProps) {
  return (
    <div className="absolute top-4 right-4 bg-gray-900/90 backdrop-blur-sm rounded-lg p-4 border border-gray-700">
      <h3 className="text-sm font-semibold text-white mb-3">Graph Stats</h3>
      <div className="space-y-2 text-xs">
        <div className="flex justify-between">
          <span className="text-gray-400">Nodes</span>
          <span className="text-white font-medium">{stats.total_nodes}</span>
        </div>
        <div className="flex justify-between">
          <span className="text-gray-400">Links</span>
          <span className="text-white font-medium">{stats.total_links}</span>
        </div>
        <div className="flex justify-between">
          <span className="text-gray-400">Active Pulses</span>
          <span className="text-purple-400 font-medium">{stats.active_pulses}</span>
        </div>
        
        {/* Node type breakdown */}
        <div className="pt-2 border-t border-gray-700">
          <div className="text-gray-400 mb-2">Node Types</div>
          <div className="grid grid-cols-2 gap-1">
            {Object.entries(stats.nodes_by_type || {}).slice(0, 6).map(([type, count]) => (
              <div key={type} className="flex items-center gap-1">
                <div 
                  className="w-2 h-2 rounded-full" 
                  style={{ backgroundColor: getNodeColor({ type } as GraphNode) }}
                />
                <span className="text-gray-300 capitalize">{type.slice(0, 8)}</span>
                <span className="text-gray-500 ml-auto">{count}</span>
              </div>
            ))}
          </div>
        </div>
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
