'use strict';
'require view';
'require poll';
'require fs';

const LOG = '/tmp/be5000-temp.log';
const DAY = 86400;

const sensors = [
	{ name: _('CPU'), color: '#f08c00' },
	{ name: _('SoC'), color: '#e64980' },
	{ name: _('Wi-Fi'), color: '#4c6ef5' }
];

/* one line per minute: "time cpu soc wifi", millidegrees or "-" */
function parse(text) {
	const rows = [];

	(text || '').split('\n').forEach(function(line) {
		const f = line.trim().split(/\s+/);
		const t = parseInt(f[0]);

		if (f.length < 4 || isNaN(t))
			return;

		rows.push({ t: t, v: f.slice(1, 4).map(function(x) {
			const m = parseInt(x);
			return isNaN(m) ? null : m / 1000;
		}) });
	});

	if (!rows.length)
		return rows;

	const from = rows[rows.length - 1].t - DAY;
	return rows.filter(function(r) { return r.t >= from; });
}

function svg(tag, attrs, children) {
	const el = document.createElementNS('http://www.w3.org/2000/svg', tag);

	for (const k in attrs)
		el.setAttribute(k, attrs[k]);

	(children || []).forEach(function(c) {
		el.appendChild(typeof c == 'string' ? document.createTextNode(c) : c);
	});

	return el;
}

function pad2(n) {
	return (n < 10 ? '0' : '') + n;
}

function chart(rows) {
	/* a narrower canvas on phones keeps the labels readable */
	const W = window.innerWidth < 600 ? 480 : 800, H = W < 800 ? 240 : 260;
	const L = 40, R = 10, T = 10, B = 26;
	const t1 = rows[rows.length - 1].t, t0 = t1 - DAY;
	let lo = Infinity, hi = -Infinity;

	rows.forEach(function(r) {
		r.v.forEach(function(v) {
			if (v == null)
				return;
			lo = Math.min(lo, v);
			hi = Math.max(hi, v);
		});
	});

	const step = hi - lo > 40 ? 10 : 5;
	lo = Math.floor(lo / step) * step;
	hi = Math.ceil(hi / step) * step;
	if (hi - lo < 2 * step)
		hi = lo + 2 * step;

	const x = function(t) { return L + (t - t0) / DAY * (W - L - R); };
	const y = function(v) { return T + (hi - v) / (hi - lo) * (H - T - B); };
	const parts = [];

	for (let v = lo; v <= hi; v += step) {
		parts.push(svg('line', { x1: L, x2: W - R, y1: y(v), y2: y(v),
			stroke: 'currentColor', 'stroke-opacity': 0.15 }));
		parts.push(svg('text', { x: L - 6, y: y(v) + 4, 'text-anchor': 'end',
			'font-size': 12, fill: 'currentColor', 'fill-opacity': 0.7 }, [ v + '°' ]));
	}

	/* a label on every third full hour */
	const first = new Date(t0 * 1000);
	first.setMinutes(0, 0, 0);

	for (let t = first.getTime() / 1000 + 3600; t <= t1; t += 3600) {
		const d = new Date(t * 1000);

		if (d.getHours() % 3)
			continue;

		parts.push(svg('line', { x1: x(t), x2: x(t), y1: T, y2: H - B,
			stroke: 'currentColor', 'stroke-opacity': 0.08 }));
		parts.push(svg('text', { x: x(t), y: H - 8, 'text-anchor': 'middle',
			'font-size': 12, fill: 'currentColor', 'fill-opacity': 0.7 },
			[ pad2(d.getHours()) + ':00' ]));
	}

	sensors.forEach(function(s, i) {
		let d = '', prev = null;

		rows.forEach(function(r) {
			const v = r.v[i];

			if (v == null) {
				prev = null;
				return;
			}

			/* no line across a gap, e.g. a reboot */
			d += (prev == null || r.t - prev > 180 ? 'M' : 'L') +
				x(r.t).toFixed(1) + ',' + y(v).toFixed(1);
			prev = r.t;
		});

		if (d)
			parts.push(svg('path', { d: d, fill: 'none', stroke: s.color,
				'stroke-width': 2, 'stroke-linejoin': 'round' }));
	});

	return svg('svg', { viewBox: '0 0 %d %d'.format(W, H), width: '100%',
		role: 'img', style: 'display:block;max-width:100%' }, parts);
}

function fmt(v) {
	return v == null ? '—' : '%.1f °C'.format(v);
}

return view.extend({
	load: function() {
		return L.resolveDefault(fs.read(LOG), '');
	},

	update: function(box, text) {
		const rows = parse(text);
		const body = [];

		if (!rows.length) {
			body.push(E('p', {}, E('em', _('No readings yet: the first one appears within a minute.'))));
		}
		else {
			const table = E('table', { 'class': 'table' }, [
				E('tr', { 'class': 'tr table-titles' }, [
					E('th', { 'class': 'th' }, _('Sensor')),
					E('th', { 'class': 'th' }, _('Now')),
					E('th', { 'class': 'th' }, _('Minimum')),
					E('th', { 'class': 'th' }, _('Maximum'))
				])
			]);

			sensors.forEach(function(s, i) {
				const vals = rows.map(function(r) { return r.v[i]; })
					.filter(function(v) { return v != null; });

				if (!vals.length)
					return;

				/* data-title labels the cells when a theme turns rows into cards */
				table.appendChild(E('tr', { 'class': 'tr' }, [
					E('td', { 'class': 'td', 'data-title': _('Sensor'), 'style': 'white-space:nowrap' }, [
						E('span', { 'style': 'color:%s'.format(s.color) }, '● '), s.name ]),
					E('td', { 'class': 'td', 'data-title': _('Now') }, fmt(rows[rows.length - 1].v[i])),
					E('td', { 'class': 'td', 'data-title': _('Minimum') }, fmt(Math.min.apply(null, vals))),
					E('td', { 'class': 'td', 'data-title': _('Maximum') }, fmt(Math.max.apply(null, vals)))
				]));
			});

			body.push(E('div', { 'style': 'padding:0.5rem 1rem' }, chart(rows)), table);
		}

		box.replaceChildren.apply(box, body);
	},

	render: function(text) {
		const box = E('div', { 'class': 'cbi-section' });

		this.update(box, text);

		poll.add(L.bind(function() {
			return L.resolveDefault(fs.read(LOG), '').then(L.bind(this.update, this, box));
		}, this), 60);

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', _('Temperature')),
			E('div', { 'class': 'cbi-map-descr' },
				_('The last 24 hours, one reading a minute. The history is kept in memory and starts over after a reboot.')),
			box
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
