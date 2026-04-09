/// UzEngine Material Editor — UE5 layout: Toolbar + Breadcrumb + Left panel (preview + details) + Graph canvas.

import { useCallback, useState } from 'react';
import { ReactFlowProvider } from '@xyflow/react';

import { useMaterialGraph } from './graph/use-material-graph';
import MaterialGraphEditor from './graph/MaterialGraphEditor';
import Toolbar from './panels/Toolbar';
import DetailsPanel from './panels/DetailsPanel';
import MaterialPreview from './panels/MaterialPreview';
import { createDefaultMaterial } from './model/material-graph';
import { parseUzmat, serializeUzmat } from './model/uzmat-io';
import type { UzMatFile } from './model/types';

export function App() {
  const [initialMat] = useState(() => createDefaultMaterial());
  const graph = useMaterialGraph(initialMat);
  const [selectedExprId, setSelectedExprId] = useState<number | null>(null);

  const selectedExpr =
    selectedExprId != null
      ? graph.material.expressions.find((e) => e.id === selectedExprId) ?? null
      : null;

  const handleLoad = useCallback(() => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.uzmat,.uzmaterial';
    input.onchange = () => {
      const file = input.files?.[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = () => {
        const text = reader.result as string;
        const mat = parseUzmat(text);
        graph.setMaterial(mat);
        setSelectedExprId(null);
      };
      reader.readAsText(file);
    };
    input.click();
  }, [graph]);

  const handleSave = useCallback(() => {
    const json = serializeUzmat(graph.material);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${graph.material.name.replace(/\s+/g, '_').toLowerCase()}.uzmat`;
    a.click();
    URL.revokeObjectURL(url);
  }, [graph.material]);

  const handleNew = useCallback(() => {
    graph.setMaterial(createDefaultMaterial());
    setSelectedExprId(null);
  }, [graph]);

  const handleMaterialChange = useCallback(
    (mat: UzMatFile) => {
      graph.material.name = mat.name;
      graph.material.domain = mat.domain;
      graph.material.shadingModel = mat.shadingModel;
      graph.material.blendMode = mat.blendMode;
      graph.material.twoSided = mat.twoSided;
      graph.setMaterial({ ...graph.material });
    },
    [graph],
  );

  const handleParamChange = useCallback(
    (
      exprId: number,
      paramType: 'scalar' | 'string' | 'int',
      key: string,
      value: number | string,
    ) => {
      if (paramType === 'string' && key === '__name') {
        const expr = graph.material.expressions.find((e) => e.id === exprId);
        if (expr) {
          expr.name = value as string;
          graph.setMaterial({ ...graph.material });
        }
        return;
      }
      graph.updateParam(exprId, paramType, key, value);
    },
    [graph],
  );

  const handleKeyDown = useCallback(
    (e: React.KeyboardEvent) => {
      if (e.ctrlKey && e.key === 's') {
        e.preventDefault();
        handleSave();
      }
    },
    [handleSave],
  );

  return (
    <div className="app-layout" onKeyDown={handleKeyDown} tabIndex={-1}>
      <Toolbar
        material={graph.material}
        onLoad={handleLoad}
        onSave={handleSave}
        onNew={handleNew}
        canUndo={graph.canUndo}
        canRedo={graph.canRedo}
        onUndo={graph.undo}
        onRedo={graph.redo}
      />

      <div className="app-main">
        {/* Left panel: Preview + Details (UE5 style) */}
        <div className="side-panel side-panel-left">
          <MaterialPreview material={graph.material} />
          <DetailsPanel
            material={graph.material}
            expression={selectedExpr}
            onMaterialChange={handleMaterialChange}
            onParamChange={handleParamChange}
          />
        </div>

        {/* Center: Graph canvas */}
        <ReactFlowProvider>
          <MaterialGraphEditor
            graph={graph}
            onSelectNode={setSelectedExprId}
          />
        </ReactFlowProvider>
      </div>
    </div>
  );
}

export default App;
