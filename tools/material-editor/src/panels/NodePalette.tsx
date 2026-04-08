/// Node palette — categorized list of expression types for drag-to-add.

import { useState } from 'react';
import {
  MatExprType,
  getExprCategory,
  type ExprCategory,
} from '../model/types';
import { CREATABLE_EXPR_TYPES, createExpression } from '../model/expression-registry';

interface Props {
  onAddNode: (type: MatExprType) => void;
}

interface PaletteEntry {
  type: MatExprType;
  displayName: string;
  category: ExprCategory;
}

const ALL_ENTRIES: PaletteEntry[] = CREATABLE_EXPR_TYPES.map((type) => {
  const factory = createExpression(type, 0);
  return {
    type,
    displayName: factory.name,
    category: getExprCategory(type),
  };
});

const CATEGORY_ORDER: ExprCategory[] = [
  'Constants',
  'Math',
  'Texture',
  'Coordinates',
  'Procedural',
  'Utility',
  'Parameters',
];

export function NodePalette({ onAddNode }: Props) {
  const [collapsed, setCollapsed] = useState<Set<ExprCategory>>(new Set());
  const [search, setSearch] = useState('');

  const lowerSearch = search.toLowerCase();
  const filtered = lowerSearch
    ? ALL_ENTRIES.filter((e) => e.displayName.toLowerCase().includes(lowerSearch))
    : ALL_ENTRIES;

  const grouped = new Map<ExprCategory, PaletteEntry[]>();
  for (const entry of filtered) {
    if (!grouped.has(entry.category)) grouped.set(entry.category, []);
    grouped.get(entry.category)!.push(entry);
  }

  function toggleCategory(cat: ExprCategory) {
    setCollapsed((prev) => {
      const next = new Set(prev);
      if (next.has(cat)) next.delete(cat);
      else next.add(cat);
      return next;
    });
  }

  return (
    <div className="panel-section">
      <div className="panel-section-title">Nodes</div>
      <input
        type="text"
        className="prop-input"
        value={search}
        onChange={(e) => setSearch(e.target.value)}
        style={{ marginBottom: 6, width: '100%' }}
      />

      {CATEGORY_ORDER.map((cat) => {
        const entries = grouped.get(cat);
        if (!entries || entries.length === 0) return null;
        const isCollapsed = collapsed.has(cat);

        return (
          <div key={cat} className="palette-category">
            <div
              className="palette-category-header"
              onClick={() => toggleCategory(cat)}
            >
              <span>{isCollapsed ? '\u25B6' : '\u25BC'}</span>
              <span>{cat} ({entries.length})</span>
            </div>
            {!isCollapsed &&
              entries.map((entry) => (
                <button
                  key={entry.type}
                  className="palette-item"
                  onClick={() => onAddNode(entry.type)}
                >
                  {entry.displayName}
                </button>
              ))}
          </div>
        );
      })}
    </div>
  );
}

export default NodePalette;
