// Reviewed for gameplay SHA256 9e7efe42...b979 (Minecraft 1.26.60.05).
// Private game symbols belong only in this version adapter.
const lholoMenuHistory = i({history: {
    action: 'PUSH', length: 1,
    list: [{pathname: '/lholo-menu', search: '', hash: '', state: ''}],
    goBack: function () { window.engine.trigger('query:subscribe/lholo.menu.close', 0); },
    go: function () { window.engine.trigger('query:subscribe/lholo.menu.close', 0); },
    push: function () {}, replace: function () {}
}});
function LHoloUseMenuHistory() { return lholoMenuHistory; }

window.__LHoloOreUiRuntime = {
    React: e,
    Facet: {createFacet: R, createStaticFacet: i, useFacetState: Ge,
        useFacetUnwrap: W, useFacetMap: F, useFacetCallback: M, useFacetEffect: G, NO_VALUE: l},
    // Same native thumbnail element/Facet sizing as dx. ItemResourceResolver
    // also accepts a URL-encoded PropertyBag and uint32 tile id for variants.
    ItemIcon: function (props) {
        const rawId = (typeof props.itemId === 'string' && props.itemId.startsWith('minecraft:'))
            ? props.itemId.slice(10)
            : (props.itemId || '');
        const side = Ec(1.6);
        const size = C(side);
        const data = props.iconData;
        return e.createElement('fast-img', {
            className: 'e96d2bbd1477d4e72350',
            style: {width: F(v => `${v}px`, [], [size]),
                height: F(v => `${v}px`, [], [size])},
            src: F((w, h) => `geo-item://${rawId}?width=${w}&height=${h}`
                + (data ? `&id=${data.id}&bag=${encodeURIComponent(data.bag)}` : ''),
                [rawId, data], [size, size])
        });
    },
    // This primitive owns focus, activation and game sounds only. All visual
    // elements and styles are authored by LHoloControls.tsx / menu.css.
    Interactive: function (props) {
        const sound = Lo('inherit');
        return e.createElement(Zs, {
            onClick: props.onClick, disabled: props.disabled, narrationText: props.label, inputLegend: false,
            soundEffectPressed: sound.press, soundEffectHovered: sound.hover,
            soundEffectFocused: sound.focus,
            className: props.className + (props.disabled ? ' lholo-control-disabled' : ''),
            classNameHovered: 'lholo-control-hovered', classNameFocused: 'lholo-control-focused',
            classNamePressed: 'lholo-control-pressed'
        }, props.children);
    },
    Scroll: function (props) { return e.createElement(zf, {floating: true, scrollbarClassName: 'lholo-hidden-scrollbar'}, props.children); },
    Completion: function () {
        return e.createElement(e.Fragment, null,
            e.createElement(DN, {type: 'contentful', renderingCompleted: true}),
            e.createElement(DN, {type: 'meaningful', renderingCompleted: true}),
            e.createElement(hN, null));
    },
    Shell: function (props) {
        return e.createElement(uw, {
            defaultRoute: '/lholo-menu', errorComponent: qN,
            semanticCollection: IP, wrapperComponent: yG,
            isKeyboardHintsEnabled: true, theme: 'menus'
        }, e.createElement(Cl, {
            route: '/lholo-menu', component: props.component, transitionComponent: cl
        }));
    }
};
