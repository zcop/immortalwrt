'use strict';
'require view';
'require rpc';
'require ui';

const callInfo = rpc.declare({
	object: 'luci.cr881x_yt921x_qos',
	method: 'get_info',
	expect: {}
});

const callStatus = rpc.declare({
	object: 'luci.cr881x_yt921x_qos',
	method: 'status',
	expect: {}
});

function fmt_num(v) {
	if (v == null)
		return '-';
	return String(v);
}

function fmt_bool(v) {
	return (+v) ? _('on') : _('off');
}

function render_table_body(tbody, ports) {
	tbody.innerHTML = '';

	if (!ports || !ports.length) {
		tbody.appendChild(E('tr', {}, [
			E('td', { colspan: 8 }, [ _('No TBF data returned') ])
		]));
		return;
	}

	for (let i = 0; i < ports.length; i++) {
		const p = ports[i];
		tbody.appendChild(E('tr', {}, [
			E('td', {}, [ 'p' + fmt_num(p.port) ]),
			E('td', {}, [ fmt_bool(p.en) ]),
			E('td', {}, [ fmt_bool(p.meter) ]),
			E('td', {}, [ fmt_bool(p.dual_rate) ]),
			E('td', {}, [ fmt_num(p.eir) ]),
			E('td', {}, [ fmt_num(Math.round(+p.rate_kbps || 0)) ]),
			E('td', {}, [ fmt_num(p.ebs) ]),
			E('td', {}, [ fmt_num(p.burst_bytes) ])
		]));
	}
}

function update_raw_box(node, st) {
	node.value = (st && st.output) ? st.output : '';
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

		const rawBox = E('textarea', {
			'class': 'cbi-input-textarea',
			'readonly': true,
			'rows': 7,
			'style': 'width:100%;font-family:monospace;'
		});

		const tbody = E('tbody');
		const table = E('table', { 'class': 'table cbi-section-table' }, [
			E('thead', {}, [
				E('tr', {}, [
					E('th', {}, [ _('Port') ]),
					E('th', {}, [ _('Shaper') ]),
					E('th', {}, [ _('Meter') ]),
					E('th', {}, [ _('Dual-rate') ]),
					E('th', {}, [ _('EIR') ]),
					E('th', {}, [ _('Rate (kbps)') ]),
					E('th', {}, [ _('EBS') ]),
					E('th', {}, [ _('Burst (bytes)') ])
				])
			]),
			tbody
		]);

		const refreshBtn = E('button', {
			'class': 'cbi-button cbi-button-neutral'
		}, [ _('Refresh') ]);

		const helperPath = E('code', {}, [ info.helper || '/usr/sbin/cr881x-yt921x-qos' ]);

		render_table_body(tbody, st.ports || []);
		update_raw_box(rawBox, st);

		refreshBtn.addEventListener('click', ui.createHandlerFn(this, function() {
			return L.resolveDefault(callStatus(), {}).then(function(next) {
				if (!next || !next.ok) {
					ui.addNotification(null,
						E('p', (next && (next.error || next.output)) || _('Failed to refresh QoS status.')),
						'error');
				}

				render_table_body(tbody, (next && next.ports) || []);
				update_raw_box(rawBox, next || {});
			});
		}));

		return E('div', {}, [
			E('div', { 'class': 'cbi-map' }, [
				E('h2', {}, [ _('CR881x QoS Offload (YT921x)') ]),
				E('div', { 'class': 'cbi-section-descr' }, [
					_('Read-only hardware TBF status from YT921x debugfs.'),
					' ',
					_('Helper: '),
					helperPath
				]),
				E('div', { 'class': 'cbi-section' }, [
					table,
					E('div', { 'class': 'cbi-value' }, [
						E('label', { 'class': 'cbi-value-title' }, [ _('Raw output') ]),
						E('div', { 'class': 'cbi-value-field' }, [ rawBox ])
					]),
					E('div', { 'class': 'cbi-page-actions' }, [
						refreshBtn
					])
				])
			])
		]);
	}
});
