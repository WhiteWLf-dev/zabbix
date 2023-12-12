/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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

package serverlistener

import (
	"encoding/json"
	"fmt"
	"time"

	"git.zabbix.com/ap/plugin-support/log"
	"zabbix.com/internal/agent"
	"zabbix.com/internal/agent/scheduler"
	"zabbix.com/pkg/version"
)

const notsupported = "ZBX_NOTSUPPORTED"

type passiveCheckRequest struct {
	Key     string `json:"key"`
	Timeout string `json:"timeout"`
}

type passiveChecksRequest struct {
	Request string                `json:"request"`
	Data    []passiveCheckRequest `json:"data"`
}

type passiveChecksResponseData struct {
	Value *string `json:"value,omitempty"`
	Error *string `json:"error,omitempty"`
}

type passiveChecksResponse struct {
	Version string                      `json:"version"`
	Data    []passiveChecksResponseData `json:"data"`
}

/*
		type activeChecksResponse struct {
			Response       string                 `json:"response"`
			Info           string                 `json:"info"`
			ConfigRevision uint64                 `json:"config_revision,omitempty"`
			Data           []*scheduler.Request   `json:"data"`
			Commands       []*agent.RemoteCommand `json:"commands"`
			Expressions    []*glexpr.Expression   `json:"regexp"`
			HistoryUpload  string                 `json:"upload"`
		}
		type AgentDataRequest struct {
		Request  string           `json:"request"`
		Data     []*AgentData     `json:"data",omitempty"`
		Commands []*AgentCommands `json:"commands,omitempty"`
		Session  string           `json:"session"`
		Host     string           `json:"host"`
		Version  string           `json:"version"`
	}
*/
type passiveCheck struct {
	conn      *passiveConnection
	scheduler scheduler.Scheduler
}

func (pc *passiveCheck) formatError(msg string) (data []byte) {
	data = make([]byte, len(notsupported)+len(msg)+1)
	copy(data, notsupported)
	copy(data[len(notsupported)+1:], msg)
	return
}

func (pc *passiveCheck) handleCheckJSON(data []byte) (err error) {
	var request passiveChecksRequest
	var timeout int

	err = json.Unmarshal(data, &request)
	if err != nil {
		return err
	}

	if len(request.Data) == 0 {
		err = fmt.Errorf("cannot find the \"key\" object in the received JSON object")
	} else {
		timeout, err = scheduler.ParseItemTimeout(request.Data[0].Timeout)
		if err != nil {
			err = fmt.Errorf("cannot find the \"Timeout\" object in the received JSON object")
		}
	}

	// direct passive check timeout is handled by the scheduler
	s, err := pc.scheduler.PerformTask(request.Data[0].Key, time.Second*time.Duration(timeout), agent.PassiveChecksClientID)

	var response passiveChecksResponse

	if err != nil {
		errString := string(err.Error())
		response = passiveChecksResponse{Version: version.Long(), Data: []passiveChecksResponseData{{Error: &errString}}}
	} else {
		response = passiveChecksResponse{Version: version.Long(), Data: []passiveChecksResponseData{{Value: &s}}}
	}

	out, err := json.Marshal(response)
	if err == nil {
		log.Debugf("sending passive check response: '%s' to '%s'", string(out), pc.conn.Address())
		_, err = pc.conn.Write(out)
	}

	if err != nil {
		log.Debugf("could not send response to server '%s': %s", pc.conn.Address(), err.Error())
	}

	return nil
}

func (pc *passiveCheck) handleCheck(data []byte) {
	// the timeout is one minute to allow see any timeout problem with passive checks
	const timeoutForSinglePassiveChecks = time.Minute
	var checkTimeout time.Duration

	err := pc.handleCheckJSON(data)
	if err == nil {
		return
	}

	checkTimeout = timeoutForSinglePassiveChecks

	// direct passive check timeout is handled by the scheduler
	s, err := pc.scheduler.PerformTask(string(data), checkTimeout, agent.PassiveChecksClientID)

	if err != nil {
		log.Debugf("sending passive check response: %s: '%s' to '%s'", notsupported, err.Error(), pc.conn.Address())
		_, err = pc.conn.Write(pc.formatError(err.Error()))
	} else {
		log.Debugf("sending passive check response: '%s' to '%s'", s, pc.conn.Address())
		_, err = pc.conn.Write([]byte(s))
	}

	if err != nil {
		log.Debugf("could not send response to server '%s': %s", pc.conn.Address(), err.Error())
	}
}
