/// Right-click context menu for adding new expression nodes.
/// Grouped by category with search filtering.

import { useEffect, useRef, useState } from 'react';
import {
  MatExprType,
  getExprCategory,
  matExprTypeToString,
  type ExprCategory,
} from '../model/types';
import { CREATABLE_EXPR_TYPES } from '../model/expression-registry';
import { createExpression } from '../model/expression-registry';

interface Props {
  x: number;
  y: number;
  onSelect: (type: MatExprType) => void;
  onClose: () => void;
}

interface MenuEntry {
  type: MatExprType;
  typeName: string;
  displayName: string;
  category: ExprCategory;
}

const ALL_ENTRIES: MenuEntry[] = CREATABLE_EXPR_TYPES.map((type) => {
  const factory = createExpression(type, 0);
  return {
    type,
    typeName: matExprTypeToString(type),
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

export function ContextMenu({ x, y, onSelect, onClose }: Props) {
  const [search, setSearch] = useState('');
  const inputRef = useRef<HTMLInputElement>(null);
  const menuRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    inputRef.current?.focus();

    function handleClickOutside(e: MouseEvent) {
      if (menuRef.current && !menuRef.current.contains(e.target as HTMLElement)) {
        onClose();
      }
    }
    function handleEscape(e: KeyboardEvent) {
      if (e.key === 'Escape') onClose();
    }

    document.addEventListener('mousedown', handleClickOutside);
    document.addEventListener('keydown', handleEscape);
    return () => {
      document.removeEventListener('mousedown', handleClickOutside);
      document.removeEventListener('keydown', handleEscape);
    };
  }, [onClose]);

  const lowerSearch = search.toLowerCase();
  const filtered = lowerSearch
    ? ALL_ENTRIES.filter(
        (e) =>
          e.displayName.toLowerCase().includes(lowerSearch) ||
          e.typeName.toLowerCase().includes(lowerSearch),
      )
    : ALL_ENTRIES;

  // Group by category
  const grouped = new Map<ExprCategory, MenuEntry[]>();
  for (const entry of filtered) {
    if (!grouped.has(entry.category)) grouped.set(entry.category, []);
    grouped.get(entry.category)!.push(entry);
  }

  return (
    <div className="context-menu" ref={menuRef} style={{ left: x, top: y }}>
      <div className="context-menu-search">
        <input
          ref={inputRef}
          type="text"
          value={search}
          onChange={(e) => setSearch(e.target.value)}
        />
      </div>

      {CATEGORY_ORDER.map((cat) => {
        const entries = grouped.get(cat);
        if (!entries || entries.length === 0) return null;

        return (
          <div key={cat}>
            <div className="context-menu-category">{cat}</div>
            {entries.map((entry) => (
              <button
                key={entry.type}
                className="context-menu-item"
                onClick={() => onSelect(entry.type)}
              >
                {entry.displayName}
              </button>
            ))}
          </div>
        );
      })}

      {filtered.length === 0 && (
        <div style={{ padding: '8px 12px', color: 'var(--text-muted)', fontSize: 12 }}>
          No matching nodes
        </div>
      )}
    </div>
  );
}

export default ContextMenu;
