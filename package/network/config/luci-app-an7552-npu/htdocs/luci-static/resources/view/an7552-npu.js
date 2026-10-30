'use strict';
'require view';
'require form';

return view.extend({
	render: function() {
		var m, s, o;

		m = new form.Map('an7552-npu', _('Advanced settings'));

		s = m.section(form.NamedSection, 'settings', 'npu', _('Wi-Fi NPU acceleration'),
			_('With the NPU on, traffic between Wi-Fi and the wired ports is forwarded in hardware at up to about 1.9 Gbit/s. Traffic that the router itself processes (VPN such as AmneziaWG or Xray, zapret, file sharing and other services on the router) then reaches the CPU at about 450 Mbit/s.') + '<br /><br />' +
			_('With the NPU off, all Wi-Fi traffic goes through the CPU: about 1.1 Gbit/s between Wi-Fi and the wired ports, but up to about 900 Mbit/s for traffic of the router itself.') + '<br /><br />' +
			_('Turn the NPU off for the fastest VPN on this router: AmneziaWG gives about 250 Mbit/s without it and about 210 Mbit/s with it. Keep it on otherwise. Applying a change restarts Wi-Fi for a few seconds.'));

		o = s.option(form.Flag, 'enabled', _('Use the NPU'),
			_('On: fastest forwarding between Wi-Fi and the wired ports. Off: fastest VPN and router services over Wi-Fi.'));
		o.default = o.enabled;
		o.rmempty = false;

		s = m.section(form.NamedSection, 'wifi', 'wifi', _('Wi-Fi latency'));

		o = s.option(form.Flag, 'gaming', _('Gaming mode'),
			_('Lower ping while someone downloads. On: other devices keep a ping of about 8 ms during a download. Off: 16-20 ms, but peak download speed is about 10% higher.'));
		o.default = o.disabled;
		o.rmempty = false;

		s = m.section(form.NamedSection, 'pppoe', 'pppoe', _('PPPoE'),
			_('Hardware routing offload cannot send a packet larger than the PPPoE MTU and leaves it to the CPU, which forwards only about 200-300 Mbit/s. The router therefore lowers the TCP segment size (MSS) of every connection through PPPoE to this value. 1452 fits the usual PPPoE MTU of 1492; use 1460 for an MTU of 1500.') + '<br /><br />' +
			_('Empty: only the MSS clamping of the firewall zone, which takes the size from the routes and misses connections that pbr or mwan3 send through PPPoE.'));

		o = s.option(form.Value, 'mss', _('TCP MSS'),
			_('From 536 to 1460. IPv6 connections get 20 bytes less.'));
		o.datatype = 'range(536,1460)';
		o.placeholder = _('off');

		s = m.section(form.NamedSection, 'mlo', 'mlo', _('Wi-Fi 7 MLO'),
			_('A separate network for Wi-Fi 7 devices on 2.4 and 5 GHz at once (Multi-Link Operation). Such a device stays connected over both bands and keeps working on 2.4 GHz if 5 GHz drops. While the MLO network is on, all networks of the router use 40 MHz on 2.4 GHz and 160 MHz on 5 GHz (channels 36-64).') + '<br /><br />' +
			_('The network uses WPA3 only. Change it here only: the router rebuilds it from these settings and removes it when MLO is off.'));

		o = s.option(form.Flag, 'enabled', _('Enable MLO network'));
		o.default = o.disabled;
		o.rmempty = false;

		o = s.option(form.Value, 'ssid', _('Network name (SSID)'));
		o.datatype = 'maxlength(32)';
		o.depends('enabled', '1');
		o.validate = function(section_id, value) {
			return value ? true : _('Expecting: non-empty value');
		};

		o = s.option(form.Value, 'key', _('Password'));
		o.password = true;
		o.datatype = 'wpakey';
		o.depends('enabled', '1');
		o.validate = function(section_id, value) {
			return (value && value.length >= 8) ? true : _('At least 8 characters');
		};

		return m.render();
	}
});
