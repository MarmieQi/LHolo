import React, { useState } from 'react';
import type {
  AssistedPlacementMode,
  ExperimentalSettingsControls,
} from '../application/useExperimentalSettings';
import type { MenuLabels } from '../platform/bridge';
import {
  LHoloButton,
  LHoloCheckbox,
  LHoloSectionGroup,
  LHoloSettingRow,
  LHoloStepButton,
  LHoloTextField,
} from '../components/LHoloControls';
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
    <LHoloTabs<PlacementTab> selected={tab} onSelect={setTab} tabs={[
      { id: 'singleplayer', label: labels['placement.tabSingleplayer'] },
      { id: 'legacy', label: labels['placement.tabLegacy'] },
    ]} />
    <div className="lholo-tab-content">
      {tab === 'singleplayer' ? <>
        <div className="lholo-settings-columns">
          <div className="lholo-settings-column">
            <LHoloSectionGroup title={labels['placement.groupModes']}>
              <LHoloCheckbox row label={labels['checkbox.manualPlace']} value={experimental.localManualEnabled}
                disabled={localModesDisabled} onChange={experimental.toggleLocalManual} />
              <LHoloCheckbox row label={labels['checkbox.easyPlace']} value={experimental.localEasyEnabled}
                disabled={localModesDisabled} onChange={experimental.toggleLocalEasy} />
              <LHoloCheckbox row label={labels['checkbox.rangePlace']} value={experimental.localRangeEnabled}
                disabled={localModesDisabled} onChange={experimental.toggleLocalRange} />
              <LHoloCheckbox row label={labels['checkbox.localPlacementLiquidsDisabled']} value={experimental.localLiquidsDisabled}
                disabled={!experimental.ready} onChange={experimental.setLocalLiquidsDisabled} />
            </LHoloSectionGroup>
          </div>
          <div className="lholo-settings-column">
            <LHoloSectionGroup title={labels['placement.groupRestock']}>
              <LHoloCheckbox row label={labels['checkbox.localShulkerRestock']} value={experimental.localShulkerRestock}
                disabled={!experimental.ready} onChange={experimental.setLocalShulkerRestock} />
              <LHoloCheckbox row label={labels['checkbox.localCreativeRestock']} value={experimental.localCreativeRestock}
                disabled={!experimental.ready} onChange={experimental.setLocalCreativeRestock} />
              <LHoloCheckbox row label={labels['checkbox.localCreativeRestockOverwrite']} value={experimental.localCreativeRestockOverwrite}
                disabled={!experimental.ready} onChange={experimental.setLocalCreativeRestockOverwrite} />
            </LHoloSectionGroup>
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
    <LHoloSectionGroup>
      <LHoloCheckbox row disabled={!worldAvailable || !experimental.canCommand} label={labels['checkbox.manualPlace']}
        value={experimental.activeMode === 'manual'}
        onChange={checked => experimental.toggleMode('manual', checked)} />
      <LHoloCheckbox row disabled={!worldAvailable || !experimental.canCommand} label={labels['checkbox.easyPlace']}
        value={experimental.activeMode === 'easy'}
        onChange={checked => experimental.toggleMode('easy', checked)} />
      <LHoloCheckbox row disabled={!worldAvailable || !experimental.canCommand} label={labels['checkbox.rangePlace']}
        value={experimental.activeMode === 'range'}
        onChange={checked => experimental.toggleMode('range', checked)} />
    </LHoloSectionGroup>

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

  return (
    <LHoloSectionGroup>
      <LHoloSettingRow label={radiusLabel}>
        <div className="lholo-stepper">
          <LHoloStepButton direction="dec" disabled={!experimental.ready}
            onClick={() => radius.step(-1)} />
          <div className="lholo-stepper-value"
            style={{ width: Math.max(3.2, Math.min(8, radius.text.length * 0.7 + 1.2)) + 'rem' }}>
            <LHoloTextField disabled={!experimental.ready} label={radiusLabel} value={radius.text} maxLength={4}
              onChange={radius.edit} onBlur={radius.commit} />
          </div>
          <LHoloStepButton direction="inc" disabled={!experimental.ready}
            onClick={() => radius.step(1)} />
        </div>
      </LHoloSettingRow>

      <LHoloSettingRow label={cooldownLabel}>
        <div className="lholo-stepper">
          <LHoloStepButton direction="dec" disabled={!experimental.ready}
            onClick={() => experimental.stepAutoPlacementCooldown(-1)} />
        <div className="lholo-stepper-value"
          style={{ width: Math.max(3.2, Math.min(8, experimental.autoPlacementCooldown.text.length * 0.7 + 1.2)) + 'rem' }}>
          <LHoloTextField disabled={!experimental.ready} label={cooldownLabel} value={experimental.autoPlacementCooldown.text} maxLength={4}
            onChange={experimental.editAutoPlacementCooldown} onBlur={experimental.commitAutoPlacementCooldown} />
        </div>
        <LHoloStepButton direction="inc" disabled={!experimental.ready}
          onClick={() => experimental.stepAutoPlacementCooldown(1)} />
      </div>
      </LHoloSettingRow>
    </LHoloSectionGroup>
  );
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
