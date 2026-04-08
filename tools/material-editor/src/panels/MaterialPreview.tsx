/// Three.js material preview — renders a sphere with MeshStandardMaterial
/// on a ground plane, with HDRI environment, shadows, and orbit controls.

import { useRef, useEffect } from 'react';
import { Canvas, useFrame } from '@react-three/fiber';
import { OrbitControls, Environment, ContactShadows } from '@react-three/drei';
import * as THREE from 'three';
import type { UzMatFile } from '../model/types';
import { MatExprType } from '../model/types';

interface Props {
  material: UzMatFile;
}

// ─────────────────────────────────────────────────────────────────────────────
// Extract PBR values from the material graph
// ─────────────────────────────────────────────────────────────────────────────

interface PBRValues {
  baseColor: [number, number, number];
  metallic: number;
  roughness: number;
  normal: [number, number, number];
  emissive: [number, number, number];
  opacity: number;
  ao: number;
}

function extractPBRValues(material: UzMatFile): PBRValues {
  const pbrOutput = material.expressions.find(
    (e) => e.type === MatExprType.PBROutput,
  );

  const defaults: PBRValues = {
    baseColor: [0.8, 0.8, 0.8],
    metallic: 0.0,
    roughness: 0.5,
    normal: [0, 0, 1],
    emissive: [0, 0, 0],
    opacity: 1.0,
    ao: 1.0,
  };

  if (!pbrOutput) return defaults;

  function resolveFloat(pinId: number, fallback: number): number {
    const pin = pbrOutput!.inputs.find((p) => p.id === pinId);
    if (!pin) return fallback;
    if (pin.connectedExprId === 0) return pin.defaultValue[0];
    const srcExpr = material.expressions.find((e) => e.id === pin.connectedExprId);
    if (!srcExpr) return pin.defaultValue[0];
    if (srcExpr.type === MatExprType.ScalarConstant) {
      return srcExpr.scalarParams['value'] ?? fallback;
    }
    if (srcExpr.type === MatExprType.ScalarParameter) {
      return srcExpr.scalarParams['default'] ?? fallback;
    }
    return fallback;
  }

  function resolveColor(pinId: number): [number, number, number] {
    const pin = pbrOutput!.inputs.find((p) => p.id === pinId);
    if (!pin) return [0, 0, 0];
    if (pin.connectedExprId === 0) {
      return [pin.defaultValue[0], pin.defaultValue[1], pin.defaultValue[2]];
    }
    const srcExpr = material.expressions.find((e) => e.id === pin.connectedExprId);
    if (!srcExpr) return [pin.defaultValue[0], pin.defaultValue[1], pin.defaultValue[2]];

    if (srcExpr.type === MatExprType.Vector3Constant) {
      return [
        srcExpr.scalarParams['x'] ?? 0,
        srcExpr.scalarParams['y'] ?? 0,
        srcExpr.scalarParams['z'] ?? 0,
      ];
    }
    if (srcExpr.type === MatExprType.Vector4Constant) {
      return [
        srcExpr.scalarParams['x'] ?? 0,
        srcExpr.scalarParams['y'] ?? 0,
        srcExpr.scalarParams['z'] ?? 0,
      ];
    }
    if (srcExpr.type === MatExprType.ColorConstant) {
      return [
        srcExpr.scalarParams['r'] ?? 0,
        srcExpr.scalarParams['g'] ?? 0,
        srcExpr.scalarParams['b'] ?? 0,
      ];
    }
    if (srcExpr.type === MatExprType.VectorParameter) {
      return [
        srcExpr.scalarParams['defaultR'] ?? 0,
        srcExpr.scalarParams['defaultG'] ?? 0,
        srcExpr.scalarParams['defaultB'] ?? 0,
      ];
    }
    return [pin.defaultValue[0], pin.defaultValue[1], pin.defaultValue[2]];
  }

  const params = material.params;

  let baseColor = resolveColor(0);
  if (params?.baseColorTint) {
    baseColor = [
      baseColor[0] * params.baseColorTint[0],
      baseColor[1] * params.baseColorTint[1],
      baseColor[2] * params.baseColorTint[2],
    ];
  }

  let metallic = resolveFloat(1, 0.0);
  if (params?.metallicMultiplier != null) metallic *= params.metallicMultiplier;

  let roughness = resolveFloat(2, 0.5);
  if (params?.roughnessMultiplier != null) roughness *= params.roughnessMultiplier;

  const normal = resolveColor(3);

  const emissive = resolveColor(4);
  if (params?.emissiveMultiplier != null) {
    emissive[0] *= params.emissiveMultiplier;
    emissive[1] *= params.emissiveMultiplier;
    emissive[2] *= params.emissiveMultiplier;
  }

  const opacity = resolveFloat(5, 1.0);
  const ao = resolveFloat(6, 1.0);

  return { baseColor, metallic, roughness, normal, emissive, opacity, ao };
}

// ─────────────────────────────────────────────────────────────────────────────
// Preview Sphere — MeshStandardMaterial with shadow casting
// ─────────────────────────────────────────────────────────────────────────────

function PreviewSphere({ material }: { material: UzMatFile }) {
  const meshRef = useRef<THREE.Mesh>(null);
  const matRef = useRef<THREE.MeshStandardMaterial>(null);

  useEffect(() => {
    if (!matRef.current) return;
    const pbr = extractPBRValues(material);
    const mat = matRef.current;
    mat.color.setRGB(pbr.baseColor[0], pbr.baseColor[1], pbr.baseColor[2]);
    mat.metalness = pbr.metallic;
    mat.roughness = pbr.roughness;
    mat.emissive.setRGB(pbr.emissive[0], pbr.emissive[1], pbr.emissive[2]);
    mat.opacity = pbr.opacity;
    mat.transparent = pbr.opacity < 1.0;
    mat.aoMapIntensity = pbr.ao;
    mat.needsUpdate = true;
  }, [material]);

  useFrame((_, delta) => {
    if (meshRef.current) {
      meshRef.current.rotation.y += delta * 0.3;
    }
  });

  const pbr = extractPBRValues(material);

  return (
    <mesh ref={meshRef} castShadow position={[0, 0, 0]}>
      <sphereGeometry args={[0.6, 64, 64]} />
      <meshStandardMaterial
        ref={matRef}
        color={new THREE.Color(pbr.baseColor[0], pbr.baseColor[1], pbr.baseColor[2])}
        metalness={pbr.metallic}
        roughness={pbr.roughness}
        emissive={new THREE.Color(pbr.emissive[0], pbr.emissive[1], pbr.emissive[2])}
        opacity={pbr.opacity}
        transparent={pbr.opacity < 1.0}
        envMapIntensity={1.0}
      />
    </mesh>
  );
}

// ─────────────────────────────────────────────────────────────────────────────
// Ground plane — dark circular disc under the sphere
// ─────────────────────────────────────────────────────────────────────────────

function GroundPlane() {
  return (
    <mesh receiveShadow rotation={[-Math.PI / 2, 0, 0]} position={[0, -0.6, 0]}>
      <circleGeometry args={[1.8, 64]} />
      <meshStandardMaterial
        color="#888888"
        roughness={0.6}
        metalness={0.0}
      />
    </mesh>
  );
}

// ─────────────────────────────────────────────────────────────────────────────
// MaterialPreview — Canvas with HDRI environment, shadows, ground
// ─────────────────────────────────────────────────────────────────────────────

export function MaterialPreview({ material }: Props) {
  return (
    <div className="preview-panel">
      <Canvas
        camera={{ position: [0, 0.4, 2.2], fov: 45 }}
        shadows
        gl={{
          antialias: true,
          toneMapping: THREE.ACESFilmicToneMapping,
          toneMappingExposure: 1.0,
        }}
        scene={{ background: new THREE.Color('#111111') }}
      >
        {/* HDRI environment — visible as background */}
        <Environment preset="city" background />

        {/* Key light with shadows */}
        <directionalLight
          position={[5, 8, 5]}
          intensity={1.5}
          castShadow
          shadow-mapSize-width={1024}
          shadow-mapSize-height={1024}
          shadow-camera-near={0.1}
          shadow-camera-far={30}
          shadow-camera-left={-5}
          shadow-camera-right={5}
          shadow-camera-top={5}
          shadow-camera-bottom={-5}
          shadow-bias={-0.002}
        />

        {/* Fill light */}
        <directionalLight position={[-3, 3, -2]} intensity={0.3} />

        {/* Ambient fill */}
        <ambientLight intensity={0.15} />

        <PreviewSphere material={material} />
        <GroundPlane />

        {/* Contact shadows for grounding the sphere */}
        <ContactShadows
          position={[0, -0.59, 0]}
          opacity={0.7}
          scale={5}
          blur={2.0}
          far={3}
          color="#000000"
        />

        <OrbitControls
          enableDamping
          dampingFactor={0.1}
          minDistance={2}
          maxDistance={8}
          target={[0, 0, 0]}
        />
      </Canvas>
    </div>
  );
}

export default MaterialPreview;
