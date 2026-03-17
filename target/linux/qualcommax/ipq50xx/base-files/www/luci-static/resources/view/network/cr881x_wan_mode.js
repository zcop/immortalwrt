'use strict';
'require view';
'require rpc';
'require ui';

const callInfo = rpc.declare({
	object: 'luci.cr881x_wan_mode',
	method: 'get_info',
	expect: {}
});

const callStatus = rpc.declare({
	object: 'luci.cr881x_wan_mode',
	method: 'status',
	expect: {}
});

const callApply = rpc.declare({
	object: 'luci.cr881x_wan_mode',
	method: 'apply',
	params: [ 'mode', 'port1', 'port2' ],
	expect: {}
});

function ui_port_label(port) {
	return (port == 'wan') ? 'lan4' : port;
}

function ui_wan_binding(port, role) {
	if (!port || port == '<unset>' || port == '<none>')
		return '<unset>';

	if (port == 'wan')
		return role;

	return ui_port_label(port) + ' (' + role + ')';
}

function ui_port_list(list) {
	if (!list)
		return '<unset>';

	return String(list).split(/\s+/).filter(Boolean).map(ui_port_label).join(' ');
}

function make_port_options(select, ports, role) {
	select.innerHTML = '';
	for (let i = 0; i < ports.length; i++) {
		const p = ports[i];
		const label = (p == 'wan' && role) ? role : ui_port_label(p);
		select.appendChild(E('option', { value: p }, label));
	}
}

function set_selected(select, value) {
	if (!value || value == '<unset>' || value == '<none>')
		return;

	for (let i = 0; i < select.options.length; i++) {
		if (select.options[i].value == value) {
			select.selectedIndex = i;
			return;
		}
	}
}

function update_status_box(node, st) {
	const text = [
		_('Board') + ': ' + (st.board || '<unknown>'),
		_('Mode') + ': ' + (st.mode || '<unknown>'),
		'br-lan ports: ' + ui_port_list(st.br_lan_ports),
		'wan1: ' + ui_wan_binding(st.wan, 'wan1'),
		'wan2: ' + ui_wan_binding(st.wan2, 'wan2')
	].join('\n');

	node.value = text;
}

function sync_mode_ui(mode, wan1Row, wan2Row) {
	wan1Row.style.display = (mode == 'all-lan') ? 'none' : '';
	wan2Row.style.display = (mode == 'dual-wan') ? '' : 'none';
}

return view.extend({
	load: function() {
		return Promise.all([
			L.resolveDefault(callInfo(), {}),
			L.resolveDefault(callStatus(), {})
		]);
	},

	render: function(data) {
		const info = data[0] || {};
		const st = data[1] || {};
		const ports = info.valid_ports || [ 'lan1', 'lan2', 'lan3', 'wan' ];
		const modes = info.valid_modes || [ 'all-lan', 'single-wan', 'dual-wan' ];

		const modeSel = E('select', { 'class': 'cbi-input-select' });
		const wan1Sel = E('select', { 'class': 'cbi-input-select' });
		const wan2Sel = E('select', { 'class': 'cbi-input-select' });
		const statusBox = E('textarea', {
			'class': 'cbi-input-textarea',
			'readonly': true,
			'rows': 6,
			'style': 'width:100%;font-family:monospace;'
		});
		const applyBtn = E('button', {
			'class': 'cbi-button cbi-button-apply'
		}, [ _('Apply') ]);
		const refreshBtn = E('button', {
			'class': 'cbi-button cbi-button-neutral'
		}, [ _('Refresh') ]);

		const wan1Row = E('div', { 'class': 'cbi-value' }, [
			E('label', { 'class': 'cbi-value-title' }, [ _('WAN port') ]),
			E('div', { 'class': 'cbi-value-field' }, [ wan1Sel ])
		]);
		const wan2Row = E('div', { 'class': 'cbi-value' }, [
			E('label', { 'class': 'cbi-value-title' }, [ _('WAN2 port') ]),
			E('div', { 'class': 'cbi-value-field' }, [ wan2Sel ])
		]);

		modeSel.appendChild(E('option', { value: 'all-lan' }, _('All ports as LAN')));
		modeSel.appendChild(E('option', { value: 'single-wan' }, _('Single WAN')));
		modeSel.appendChild(E('option', { value: 'dual-wan' }, _('Dual WAN')));

		make_port_options(wan1Sel, ports, 'wan1');
		make_port_options(wan2Sel, ports, 'wan2');
		update_status_box(statusBox, st);

		const knownMode = (modes.indexOf(st.mode) >= 0) ? st.mode : 'single-wan';
		modeSel.value = knownMode;
		set_selected(wan1Sel, st.wan);
		set_selected(wan2Sel, st.wan2);
		sync_mode_ui(modeSel.value, wan1Row, wan2Row);

		modeSel.addEventListener('change', function() {
			sync_mode_ui(modeSel.value, wan1Row, wan2Row);
		});

		refreshBtn.addEventListener('click', ui.createHandlerFn(this, function() {
			return L.resolveDefault(callStatus(), {}).then(function(next) {
				update_status_box(statusBox, next || {});
			});
		}));

		applyBtn.addEventListener('click', ui.createHandlerFn(this, function() {
			const mode = modeSel.value;
			const p1 = wan1Sel.value;
			const p2 = wan2Sel.value;

			applyBtn.disabled = true;
			return callApply(mode, p1, p2).then(function(res) {
				if (!res || !res.ok) {
					ui.addNotification(null,
						E('p', (res && (res.error || res.output)) || _('Failed to apply WAN mode.')),
						'error');
					return;
				}

				update_status_box(statusBox, res);
				set_selected(wan1Sel, res.wan);
				set_selected(wan2Sel, res.wan2);
				modeSel.value = res.mode || modeSel.value;
				sync_mode_ui(modeSel.value, wan1Row, wan2Row);
				ui.addNotification(null,
					E('p', _('WAN mode applied. Network restart is scheduled.')),
					'info');
			}).catch(function(err) {
				const msg = String((err && (err.message || err)) || '');
				if (msg.toLowerCase().indexOf('aborted') >= 0) {
					ui.addNotification(null,
						E('p', _('Request was interrupted while network restarted. Wait a few seconds, then click Refresh.')),
						'warning');
					return;
				}

				ui.addNotification(null, E('p', msg || _('Apply failed.')), 'error');
			}).finally(function() {
				applyBtn.disabled = false;
			});
		}));

		return E('div', {}, [
			E('div', { 'class': 'cbi-map' }, [
				E('h2', {}, [ _('CR881x WAN Mode') ]),
				E('div', { 'class': 'cbi-section-descr' }, [
					_('Switch port roles without CLI. Uses /usr/sbin/cr881x-wan-mode and restarts network/firewall.')
				]),
				E('div', { 'class': 'cbi-section' }, [
					E('div', { 'class': 'cbi-value' }, [
						E('label', { 'class': 'cbi-value-title' }, [ _('Mode') ]),
						E('div', { 'class': 'cbi-value-field' }, [ modeSel ])
					]),
					wan1Row,
					wan2Row,
					E('div', { 'class': 'cbi-value' }, [
						E('label', { 'class': 'cbi-value-title' }, [ _('Current status') ]),
						E('div', { 'class': 'cbi-value-field' }, [ statusBox ])
					]),
					E('div', { 'class': 'cbi-page-actions' }, [
						applyBtn,
						' ',
						refreshBtn
					])
				])
			])
		]);
	}
});
