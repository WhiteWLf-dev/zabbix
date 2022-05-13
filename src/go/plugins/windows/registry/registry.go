// +build windows
/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

package registry

import (
	"zabbix.com/pkg/plugin"
	"zabbix.com/pkg/registry"
)

// Plugin -
type Plugin struct {
	plugin.Base
}

var impl Plugin

// Export -
func (p *Plugin) Export(key string, params []string, ctx plugin.ContextProvider) (result interface{}, err error) {
	switch key {
	case "registry.data":
		return registry.GetValue(params)
	case "registry.get":
		return registry.Discover(params)
	default:
		return nil, plugin.UnsupportedMetricError
	}
}

func init() {
	plugin.RegisterMetrics(&impl, "Registry",
		"registry.data", "Return value of the registry key.",
		"registry.get", "Discover registry key and its subkeys.",
	)
}
