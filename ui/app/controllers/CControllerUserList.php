<?php
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


class CControllerUserList extends CController {

	protected function init() {
		$this->disableSIDValidation();
	}

	protected function checkInput() {
		$fields = [
			'sort' =>				'in username,name,surname,role_name',
			'sortorder' =>			'in '.ZBX_SORT_DOWN.','.ZBX_SORT_UP,
			'uncheck' =>			'in 1',
			'filter_set' =>			'in 1',
			'filter_rst' =>			'in 1',
			'filter_username' =>	'string',
			'filter_name' =>		'string',
			'filter_surname' =>		'string',
			'filter_roles' =>		'array_id',
			'filter_usrgrpids'=>	'array_id',
			'filter_source' =>		'id',
			'page' =>				'ge 1'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(new CControllerResponseFatal());
		}

		return $ret;
	}

	protected function checkPermissions() {
		return $this->checkAccess(CRoleHelper::UI_ADMINISTRATION_USERS);
	}

	protected function doAction() {
		$sortfield = $this->getInput('sort', CProfile::get('web.user.sort', 'username'));
		$sortorder = $this->getInput('sortorder', CProfile::get('web.user.sortorder', ZBX_SORT_UP));
		CProfile::update('web.user.sort', $sortfield, PROFILE_TYPE_STR);
		CProfile::update('web.user.sortorder', $sortorder, PROFILE_TYPE_STR);

		if ($this->hasInput('filter_set')) {
			CProfile::update('web.user.filter_username', $this->getInput('filter_username', ''), PROFILE_TYPE_STR);
			CProfile::update('web.user.filter_name', $this->getInput('filter_name', ''), PROFILE_TYPE_STR);
			CProfile::update('web.user.filter_surname', $this->getInput('filter_surname', ''), PROFILE_TYPE_STR);
			CProfile::updateArray('web.user.filter_roles', $this->getInput('filter_roles', []), PROFILE_TYPE_ID);
			CProfile::updateArray('web.user.filter_usrgrpids', $this->getInput('filter_usrgrpids', []),
				PROFILE_TYPE_ID
			);
			CProfile::update('web.user.filter_source', $this->getInput('filter_source', ''), PROFILE_TYPE_STR);
		}
		elseif ($this->hasInput('filter_rst')) {
			CProfile::delete('web.user.filter_username');
			CProfile::delete('web.user.filter_name');
			CProfile::delete('web.user.filter_surname');
			CProfile::deleteIdx('web.user.filter_roles');
			CProfile::deleteIdx('web.user.filter_usrgrpids');
			CProfile::delete('web.user.filter_source');
		}

		$filter = [
			'username' => CProfile::get('web.user.filter_username', ''),
			'name' => CProfile::get('web.user.filter_name', ''),
			'surname' => CProfile::get('web.user.filter_surname', ''),
			'roles' => CProfile::getArray('web.user.filter_roles', []),
			'usrgrpids' => CProfile::getArray('web.user.filter_usrgrpids', []),
			'source' => CProfile::get('web.user.filter_source', '')
		];

		$data = [
			'uncheck' => $this->hasInput('uncheck'),
			'sort' => $sortfield,
			'sortorder' => $sortorder,
			'filter' => $filter,
			'profileIdx' => 'web.user.filter',
			'active_tab' => CProfile::get('web.user.filter.active', 1),
			'sessions' => [],
			'allowed_ui_user_groups' => $this->checkAccess(CRoleHelper::UI_ADMINISTRATION_USER_GROUPS)
		];

		$data['filter']['roles'] = $filter['roles']
			? CArrayHelper::renameObjectsKeys(API::Role()->get([
				'output' => ['roleid', 'name'],
				'roleids' => $filter['roles']
			]), ['roleid' => 'id'])
			: [];

		$data['filter']['usrgrpids'] = $filter['usrgrpids']
			? CArrayHelper::renameObjectsKeys(API::UserGroup()->get([
				'output' => ['usrgrpid', 'name'],
				'usrgrpids' => $filter['usrgrpids']
			]), ['usrgrpid' => 'id'])
			: [];

		$data['source'] = [_('All'), _('Internal'), _('LDAP'), _('SAML')];

		$limit = CSettingsHelper::get(CSettingsHelper::SEARCH_LIMIT) + 1;
		$data['users'] = API::User()->get([
			'output' => ['userid', 'username', 'name', 'surname', 'autologout', 'attempt_failed', 'roleid',
				'userdirectoryid'
			],
			'selectUsrgrps' => ['name', 'gui_access', 'users_status'],
			'selectRole' => ['name'],
			'search' => [
				'username' => ($filter['username'] === '') ? null : $filter['username'],
				'name' => ($filter['name'] === '') ? null : $filter['name'],
				'surname' => ($filter['surname'] === '') ? null : $filter['surname']
			],
			'filter' => [
				'roleid' => ($filter['roles'] == -1) ? null : $filter['roles']
			],
			'usrgrpids' => ($filter['usrgrpids'] == []) ? null : $filter['usrgrpids'],
			'getAccess' => true,
			'limit' => $limit
		]);

		foreach ($data['users'] as &$user) {
			$user['role_name'] = $user['role']['name'];

			if ($user['userdirectoryid'] == 0) {
				$user['source'] = _('Internal');
			}
			else {
				$idp_type = API::UserDirectory()->get([
					'output' => ['idp_type'],
					'userdirectoryids' => $user['userdirectoryid']
				]);

				if ($idp_type[0]['idp_type'] == 1) {
					$user['source'] = _('LDAP');
				}
				else {
					$user['source'] = _('SAML');
				}
			}
		}
		unset($user);

		if ($filter['source'] != 0) {
			$filter_source_name = $data['source'][$filter['source']];
			$data['users'] = array_filter($data['users'], function ($user) use ($filter_source_name) {
				return ($user['source'] === $filter_source_name);
			});
		}

		// data sort and pager
		CArrayHelper::sort($data['users'], [['field' => $sortfield, 'order' => $sortorder]]);

		$page_num = getRequest('page', 1);
		CPagerHelper::savePage('user.list', $page_num);
		$data['paging'] = CPagerHelper::paginate($page_num, $data['users'], $sortorder,
			(new CUrl('zabbix.php'))->setArgument('action', $this->getAction())
		);

		// set default lastaccess time to 0
		foreach ($data['users'] as $user) {
			$data['sessions'][$user['userid']] = ['lastaccess' => 0];
		}

		$db_sessions = DBselect(
			'SELECT s.userid,MAX(s.lastaccess) AS lastaccess,s.status'.
			' FROM sessions s'.
			' WHERE '.dbConditionInt('s.userid', array_column($data['users'], 'userid')).
			' GROUP BY s.userid,s.status'
		);
		while ($db_session = DBfetch($db_sessions)) {
			if ($data['sessions'][$db_session['userid']]['lastaccess'] < $db_session['lastaccess']) {
				$data['sessions'][$db_session['userid']] = $db_session;
			}
		}

		$data['config'] = [
			'login_attempts' => CSettingsHelper::get(CSettingsHelper::LOGIN_ATTEMPTS),
			'max_in_table' => CSettingsHelper::get(CSettingsHelper::MAX_IN_TABLE)
		];

		$response = new CControllerResponseData($data);
		$response->setTitle(_('Configuration of users'));
		$this->setResponse($response);
	}
}
