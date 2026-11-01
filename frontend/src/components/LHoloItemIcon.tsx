import React from 'react';
import { NativeItemIcon, type ItemIconData } from '../platform/native';

// Rows forward native identity and opaque visual data. Runtime rendering and
// private URL syntax stay in the adapter; unresolved identities keep an empty slot.
export function LHoloItemIcon({ iconId, iconData }: { iconId?: string; iconData?: ItemIconData }) {
  return <div className="lholo-material-icon">
    {iconId ? <NativeItemIcon itemId={iconId} iconData={iconData} /> : <div className="lholo-material-icon-empty" />}
  </div>;
}
