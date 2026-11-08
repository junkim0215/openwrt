'use strict';
'require view';
'require uci';
'require ui';

function awgInterfaces() {
	return uci.sections('network', 'interface').filter(function(s) {
		return s.proto == 'amneziawg';
	});
}

function zoneOf(name) {
	var zone = uci.sections('firewall', 'zone').filter(function(z) {
		return L.toArray(z.network).indexOf(name) > -1;
	})[0];

	return zone ? zone.name : null;
}

/* themes differ: Argon drops list markers and section padding */
var BOX = 'padding:0.5rem 1rem 1rem';

function freeName() {
	for (var i = 0; ; i++)
		if (!uci.get('network', 'awg' + i))
			return 'awg' + i;
}

return view.extend({
	load: function() {
		return Promise.all([ uci.load('network'), uci.load('firewall') ]);
	},

	handleCreate: function(name) {
		var wan = uci.sections('firewall', 'zone').filter(function(z) {
			return z.name == 'wan';
		})[0];

		uci.add('network', 'interface', name);
		uci.set('network', name, 'proto', 'amneziawg');

		if (wan) {
			var nets = L.toArray(wan.network);

			if (nets.indexOf(name) < 0)
				uci.set('firewall', wan['.name'], 'network', nets.concat([ name ]));
		}

		return uci.save().then(function() {
			window.location.href = L.url('admin/network/network');
		});
	},

	render: function() {
		var ifaces = awgInterfaces(),
		    name = freeName(),
		    rows = ifaces.map(function(s) {
			var peers = uci.sections('network', 'amneziawg_' + s['.name']),
			    hosts = peers.map(function(p) {
				return p.endpoint_host ? p.endpoint_host + (p.endpoint_port ? ':' + p.endpoint_port : '') : null;
			}).filter(function(h) { return h; });

			return E('tr', { 'class': 'tr' }, [
				E('td', { 'class': 'td' }, E('strong', s['.name'])),
				E('td', { 'class': 'td' }, s.disabled == '1' ? _('Disabled') : _('Enabled')),
				E('td', { 'class': 'td' }, String(peers.length)),
				E('td', { 'class': 'td' }, hosts.join(', ') || '—'),
				E('td', { 'class': 'td' }, zoneOf(s['.name']) || _('none'))
			]);
		});

		var list = rows.length ? E('table', { 'class': 'table' }, [
			E('tr', { 'class': 'tr table-titles' }, [
				E('th', { 'class': 'th' }, _('Interface')),
				E('th', { 'class': 'th' }, _('State')),
				E('th', { 'class': 'th' }, _('Peers')),
				E('th', { 'class': 'th' }, _('Server')),
				E('th', { 'class': 'th' }, _('Firewall zone'))
			])
		].concat(rows)) : E('p', {}, E('em', _('There are no AmneziaWG interfaces yet.')));

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', _('AmneziaWG')),
			E('div', { 'class': 'cbi-map-descr' }, _('AmneziaWG is set up as a network interface. Its settings are in Network → Interfaces, the handshakes and the traffic in Status → AmneziaWG.')),
			E('div', { 'class': 'cbi-section' }, [
				list,
				E('div', { 'class': 'right', 'style': BOX }, [
					E('a', { 'class': 'btn cbi-button cbi-button-neutral', 'href': L.url('admin/status/amneziawg') }, _('Status')), ' ',
					E('a', { 'class': 'btn cbi-button cbi-button-neutral', 'href': L.url('admin/network/network') }, _('Interfaces'))
				])
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', _('New client')),
				E('ol', { 'style': 'margin:0;padding:0.5rem 1rem 0.5rem 2.5rem' }, [
					E('li', { 'style': 'list-style-type:decimal' }, _('Create the interface: %s appears in Network → Interfaces, in the wan firewall zone.').format(name)),
					E('li', { 'style': 'list-style-type:decimal' }, _('Click Edit next to %s.').format(name)),
					E('li', { 'style': 'list-style-type:decimal' }, _('On the General Settings tab click Load configuration…, choose the .conf file (in the AmneziaVPN app: share the connection in the AmneziaWG native format) and click Import settings.')),
					E('li', { 'style': 'list-style-type:decimal' }, _('On the Peers tab edit the peer and turn on Route Allowed IPs: with AllowedIPs = 0.0.0.0/0 all traffic goes into the tunnel.')),
					E('li', { 'style': 'list-style-type:decimal' }, _('Click Save & Apply.'))
				]),
				E('div', { 'class': 'right', 'style': BOX }, E('button', {
					'class': 'btn cbi-button-add',
					'click': ui.createHandlerFn(this, 'handleCreate', name)
				}, _('Create interface %s').format(name)))
			])
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
