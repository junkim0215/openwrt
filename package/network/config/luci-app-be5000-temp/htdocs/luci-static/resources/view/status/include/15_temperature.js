'use strict';
'require baseclass';
'require fs';

const thermal = [
	[ _('CPU'), '/sys/class/thermal/thermal_zone0/temp' ],
	[ _('SoC'), '/sys/class/thermal/thermal_zone1/temp' ]
];

/* both bands report the temperature of the same Wi-Fi chip; rpcd checks
 * the ACL against both these links and their resolved /sys/devices path
 */
const hwmon = [ 0, 1, 2, 3 ].map(i => '/sys/class/hwmon/hwmon%d/'.format(i));

function read(path) {
	return L.resolveDefault(fs.read(path), null);
}

return baseclass.extend({
	title: _('Temperature'),

	load: function() {
		return Promise.all([
			Promise.all(thermal.map(s => read(s[1]))),
			Promise.all(hwmon.map(d => Promise.all([ read(d + 'name'), read(d + 'temp1_input') ])))
		]);
	},

	render: function(data) {
		const rows = thermal.map((s, i) => [ s[0], data[0][i] ]);
		const wifi = data[1].find(h => h[0] && h[0].indexOf('mt7996') == 0 && h[1]);

		if (wifi)
			rows.push([ _('Wi-Fi'), wifi[1] ]);

		const table = E('table', { 'class': 'table' });

		rows.forEach(function(r) {
			const t = parseInt(r[1]);

			if (isNaN(t))
				return;

			table.appendChild(E('tr', { 'class': 'tr' }, [
				E('td', { 'class': 'td left', 'width': '33%' }, [ r[0] ]),
				E('td', { 'class': 'td left' }, [ '%.1f °C'.format(t / 1000) ])
			]));
		});

		return table;
	}
});
