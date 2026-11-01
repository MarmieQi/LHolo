// Reviewed for gameplay SHA256 abf73f19...e5cc (Minecraft 1.26.51.01).
// Private game symbols belong only in this version adapter.
const lholoMenuHistory = a.createStaticFacet({history: {
    action: 'PUSH', length: 1,
    list: [{pathname: '/lholo-menu', search: '', hash: '', state: ''}],
    goBack: function () { window.engine.trigger('query:subscribe/lholo.menu.close', 0); },
    go: function () { window.engine.trigger('query:subscribe/lholo.menu.close', 0); },
    push: function () {}, replace: function () {}
}});
function LHoloUseMenuHistory() { return lholoMenuHistory; }

window.__LHoloOreUiRuntime = {
    React: e,
    Facet: a,
    // Same native thumbnail element/Facet sizing as u_. ItemResourceResolver
    // also accepts a URL-encoded PropertyBag and uint32 tile id for variants.
    ItemIcon: function (props) {
        const rawId = (typeof props.itemId === 'string' && props.itemId.startsWith('minecraft:'))
            ? props.itemId.slice(10)
            : (props.itemId || '');
        const side = uc(1.6);
        const size = a.useFacetWrap(side);
        const data = props.iconData;
        return e.createElement('fast-img', {
            className: 'e96d2bbd1477d4e72350',
            style: {width: a.useFacetMap(v => `${v}px`, [], [size]),
                height: a.useFacetMap(v => `${v}px`, [], [size])},
            src: a.useFacetMap((w, h) => `geo-item://${rawId}?width=${w}&height=${h}`
                + (data ? `&id=${data.id}&bag=${encodeURIComponent(data.bag)}` : ''),
                [rawId, data], [size, size])
        });
    },
    // This primitive owns focus, activation and game sounds only. All visual
    // elements and styles are authored by LHoloControls.tsx / menu.css.
    Interactive: function (props) {
        const sound = no('inherit');
        return e.createElement(Us, {
            onClick: props.onClick, disabled: props.disabled, narrationText: props.label, inputLegend: false,
            soundEffectPressed: sound.press, soundEffectHovered: sound.hover,
            soundEffectFocused: sound.focus,
            className: props.className + (props.disabled ? ' lholo-control-disabled' : ''),
            classNameHovered: 'lholo-control-hovered', classNameFocused: 'lholo-control-focused',
            classNamePressed: 'lholo-control-pressed'
        }, props.children);
    },
    Scroll: function (props) { return e.createElement(cf, {floating: true, scrollbarClassName: 'lholo-hidden-scrollbar'}, props.children); },
    Completion: function () {
        return e.createElement(e.Fragment, null,
            e.createElement(qP, {type: 'contentful', renderingCompleted: true}),
            e.createElement(qP, {type: 'meaningful', renderingCompleted: true}),
            e.createElement(LP, null));
    },
    Shell: function (props) {
        return e.createElement(CF, {
            defaultRoute: '/lholo-menu', errorComponent: lT,
            semanticCollection: RF, wrapperComponent: aL,
            isKeyboardHintsEnabled: true, theme: 'menus'
        }, e.createElement(Ro, {
            route: '/lholo-menu', component: props.component, transitionComponent: Po
        }));
    }
};
