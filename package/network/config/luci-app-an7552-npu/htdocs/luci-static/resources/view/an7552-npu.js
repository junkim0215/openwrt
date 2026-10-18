'use strict';
'require view';
'require form';

return view.extend({
	render: function() {
		var m, s, o;

		m = new form.Map('an7552-npu', _('Wi-Fi NPU acceleration'),
			_('With the NPU on, traffic between Wi-Fi and the wired ports is forwarded in hardware at up to about 1.9 Gbit/s. Traffic that the router itself processes (VPN such as AmneziaWG or Xray, zapret, file sharing and other services on the router) then reaches the CPU at only about 150 Mbit/s.') + '<br /><br />' +
			_('With the NPU off, all Wi-Fi traffic goes through the CPU: about 1.1 Gbit/s between Wi-Fi and the wired ports, but up to about 900 Mbit/s for traffic of the router itself.') + '<br /><br />' +
			_('Turn the NPU off if Wi-Fi clients go through a VPN on this router. Keep it on if the router only forwards traffic. Applying a change restarts Wi-Fi for a few seconds.'));

		s = m.section(form.NamedSection, 'settings', 'npu');

		o = s.option(form.Flag, 'enabled', _('Use the NPU'),
			_('On: fastest forwarding between Wi-Fi and the wired ports. Off: fastest VPN and router services over Wi-Fi.'));
		o.default = o.enabled;
		o.rmempty = false;

		return m.render();
	}
});
