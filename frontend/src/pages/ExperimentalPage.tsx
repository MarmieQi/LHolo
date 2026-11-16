import React, { useState } from 'react';
import type {
  AssistedPlacementMode,
  ExperimentalSettingsControls,
} from '../application/useExperimentalSettings';
import type { MenuLabels } from '../platform/bridge';
import { LHoloButton, LHoloCheckbox, LHoloTextField } from '../components/LHoloControls';
import { LHoloTabs } from '../components/LHoloTabs';
import { NativeScrollView } from '../platform/native';

type PlacementTab = 'singleplayer' | 'legacy';
type ExperimentalPageProps = {
  labels: MenuLabels; experimental: ExperimentalSettingsControls; worldAvailable: boolean;
};

export function ExperimentalPage(props: ExperimentalPageProps) {
  const { labels, experimental, worldAvailable } = props;
  const [tab, setTab] = useState<PlacementTab>('singleplayer');
  const localModesDisabled = !worldAvailable || !experimental.localRangeAvailable || !experimental.canCommand;

  return <>
    <div className="lholo-section-title">{labels['section.assistedPlacement']}</div>
    <LHoloTabs<PlacementTab> selected={tab} onSelect={setTab} tabs={[
      { id: 'singleplayer', label: labels['placement.tabSingleplayer'] },
      { id: 'legacy', label: labels['placement.tabLegacy'] },
    ]} />
    <div className="lholo-tab-content">
      {tab === 'singleplayer' ? <>
        <div className="lholo-settings-columns">
          <div className="lholo-settings-column">
            <div className="lholo-settings-group-title">{labels['placement.groupModes']}</div>
            <LHoloCheckbox label={labels['checkbox.manualPlace']} value={experimental.localManualEnabled}
              disabled={localModesDisabled} onChange={experimental.toggleLocalManual} />
            <LHoloCheckbox label={labels['checkbox.easyPlace']} value={experimental.localEasyEnabled}
              disabled={localModesDisabled} onChange={experimental.toggleLocalEasy} />
            <LHoloCheckbox label={labels['checkbox.rangePlace']} value={experimental.localRangeEnabled}
              disabled={localModesDisabled} onChange={experimental.toggleLocalRange} />
            <LHoloCheckbox label={labels['checkbox.localPlacementLiquidsDisabled']} value={experimental.localLiquidsDisabled}
              disabled={!experimental.ready} onChange={experimental.setLocalLiquidsDisabled} />
          </div>
          <div className="lholo-settings-column">
            <div className="lholo-settings-group-title">{labels['placement.groupRestock']}</div>
            <LHoloCheckbox label={labels['checkbox.localShulkerRestock']} value={experimental.localShulkerRestock}
              disabled={!experimental.ready} onChange={experimental.setLocalShulkerRestock} />
            <LHoloCheckbox label={labels['checkbox.localCreativeRestock']} value={experimental.localCreativeRestock}
              disabled={!experimental.ready} onChange={experimental.setLocalCreativeRestock} />
            <LHoloCheckbox label={labels['checkbox.localCreativeRestockOverwrite']} value={experimental.localCreativeRestockOverwrite}
              disabled={!experimental.ready} onChange={experimental.setLocalCreativeRestockOverwrite} />
          </div>
        </div>
        <div className="lholo-placement-options">
          <RangeSettings labels={labels} experimental={experimental} local />
        </div>
      </> : <LegacyPlacement {...props} />}
    </div>
  </>;
}

function LegacyPlacement({ labels, experimental, worldAvailable }: ExperimentalPageProps) {
  const infoButtonLabel = experimental.experimentalConsent
    ? labels['button.experimentalInfoView']
    : labels['button.experimentalInfoOpen'];

  const modeKeyById: Record<AssistedPlacementMode, string> = {
    manual: 'mode.manual',
    easy: 'mode.easy',
    range: 'mode.range',
  };

  return <>
    <div className="lholo-action-row">
      <LHoloButton label={infoButtonLabel} className="lholo-danger-button"
        onClick={experimental.openInfoModal} />
    </div>
    <div className="lholo-render-check">
      <LHoloCheckbox disabled={!worldAvailable || !experimental.canCommand} label={labels['checkbox.manualPlace']}
        value={experimental.activeMode === 'manual'}
        onChange={checked => experimental.toggleMode('manual', checked)} />
    </div>
    <div className="lholo-render-check">
      <LHoloCheckbox disabled={!worldAvailable || !experimental.canCommand} label={labels['checkbox.easyPlace']}
        value={experimental.activeMode === 'easy'}
        onChange={checked => experimental.toggleMode('easy', checked)} />
    </div>
    <div className="lholo-render-check">
      <LHoloCheckbox disabled={!worldAvailable || !experimental.canCommand} label={labels['checkbox.rangePlace']}
        value={experimental.activeMode === 'range'}
        onChange={checked => experimental.toggleMode('range', checked)} />
    </div>

    {experimental.activeMode ? (
      <div className="lholo-status">
        {labels['hint.modeEnabled'].replace('%s', labels[modeKeyById[experimental.activeMode]])}
      </div>
    ) : !experimental.experimentalConsent ? (
      <div className="lholo-muted">{labels['hint.assistedDisabledByConsent']}</div>
    ) : (
      <div className="lholo-muted">{labels['hint.assistedDisabled']}</div>
    )}

    <RangeSettings labels={labels} experimental={experimental} />
  </>;
}

function RangeSettings({ labels, experimental, local }: Pick<ExperimentalPageProps, 'labels' | 'experimental'> & {local?: boolean}) {
  const radiusLabel = labels[local ? 'label.localPlacementRadius' : 'label.placementRadius'];
  const radius = experimental.radius(local ? 'localPlacementRadius' : 'placementRadius');
  const cooldownLabel = labels['label.autoPlacementCooldown'];

  return <>
    <div className="lholo-experimental-row lholo-setting-row">
      <div className="lholo-experimental-label">{radiusLabel}</div>
      <div className="lholo-stepper">
        <LHoloButton disabled={!experimental.ready} label="-" className="lholo-stepper-button"
          onClick={() => radius.step(-1)} />
        <div className="lholo-stepper-value"
          style={{ width: Math.max(3.2, Math.min(8, radius.text.length * 0.7 + 1.2)) + 'rem' }}>
          <LHoloTextField disabled={!experimental.ready} label={radiusLabel} value={radius.text} maxLength={4}
            onChange={radius.edit} onBlur={radius.commit} />
        </div>
        <LHoloButton disabled={!experimental.ready} label="+" className="lholo-stepper-button"
          onClick={() => radius.step(1)} />
      </div>
    </div>

    <div className="lholo-experimental-row lholo-setting-row">
      <div className="lholo-experimental-label">{cooldownLabel}</div>
      <div className="lholo-stepper">
        <LHoloButton disabled={!experimental.ready} label="-" className="lholo-stepper-button"
          onClick={() => experimental.stepAutoPlacementCooldown(-1)} />
        <div className="lholo-stepper-value"
          style={{ width: Math.max(3.2, Math.min(8, experimental.autoPlacementCooldown.text.length * 0.7 + 1.2)) + 'rem' }}>
          <LHoloTextField disabled={!experimental.ready} label={cooldownLabel} value={experimental.autoPlacementCooldown.text} maxLength={4}
            onChange={experimental.editAutoPlacementCooldown} onBlur={experimental.commitAutoPlacementCooldown} />
        </div>
        <LHoloButton disabled={!experimental.ready} label="+" className="lholo-stepper-button"
          onClick={() => experimental.stepAutoPlacementCooldown(1)} />
      </div>
    </div>
  </>;
}

export function ExperimentalInfoModal({ labels, experimental }: {
  labels: MenuLabels; experimental: ExperimentalSettingsControls;
}) {
  if (experimental.pendingConsentFeature === null) return null;

  return <div className="lholo-modal-backdrop">
    <div className="lholo-modal">
      <div className="lholo-modal-title">{labels['modal.titleExperimental']}</div>
      <div className="lholo-transform-divider" />
      <div className="lholo-modal-body-viewport"><NativeScrollView>
        <div className="lholo-modal-body">
          <div className="lholo-modal-paragraph">{labels['experimental.warningIntro']}</div>
          <div className="lholo-modal-paragraph">{labels['experimental.warningAntiCheat']}</div>
          <div className="lholo-modal-paragraph">{labels['experimental.warningAllowed']}</div>
          <div className="lholo-modal-paragraph">{labels['experimental.warningConsequence']}</div>
          <div className="lholo-modal-paragraph">{labels['experimental.warningResponsibility']}</div>
          <div className="lholo-modal-paragraph">{labels['experimental.warningManualSafe']}</div>
        </div>
      </NativeScrollView></div>
      <div className="lholo-transform-divider" />
      <div className="lholo-action-row">
        {experimental.experimentalConsent ? (
          <LHoloButton label={labels['button.close']} onClick={experimental.closeModal} />
        ) : <>
          <LHoloButton disabled={!experimental.canCommand} label={labels['button.consentEnable']} className="lholo-danger-button"
            onClick={experimental.confirmConsent} />
          <LHoloButton label={labels['button.cancel']} onClick={experimental.closeModal} />
        </>}
      </div>
    </div>
  </div>;
}
