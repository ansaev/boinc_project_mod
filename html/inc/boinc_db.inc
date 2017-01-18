<?php
// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2008 University of California
//
// BOINC is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// BOINC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with BOINC.  If not, see <http://www.gnu.org/licenses/>.

function incs() {
    $d = dirname(__FILE__);
    require_once("$d/db_conn.inc");
    require_once("$d/util_basic.inc");
}

incs();

class BoincDb extends DbConn {
    static $instance;

    // connect to the database (possibly to a read-only replica)
    // NOTE: choice of replica can be made only at the page level.
    // If there's a page that's guaranteed to do only reads, put
    // BoincDb::get(true);
    // at the top of it.
    //
    // Specify a $fallback_mode that is used when $readonly is true:
    // 0: default, use db_user if no replica_db_user is specified,
    //    first try replica_db_host (if specified) then db_host
    // 1: only use replica_db_user, first try replica_db_host then db_host
    // 2: only use replica_db_user, only try replica_db_host
    // can be set projectwide using <replica_fallback_mode>
    //
    static function get_aux($readonly, $fallback_mode = 0) {
        $config = get_config();
        $user = parse_config($config, '<db_user>');
        $passwd = parse_config($config, '<db_passwd>');
        $host = parse_config($config, '<db_host>');
        $replica_host = parse_config($config, '<replica_db_host>');
        $name = parse_config($config, '<db_name>');
        $fm = parse_config($config, '<replica_fallback_mode>');
        if ($fm) {
            // override parameter with config.xml setting
            $fallback_mode = $fm;
        }
        if ($host == null) {
            $host = "localhost";
        }
        $instance = new DbConn();
        if ($readonly) {
            if (($fallback_mode > 0) && (!$replica_host)) {
                error_log("BoincDb::get_aux(): <replica_db_host> required for \$fallback_mode > 0 (giving up)");
                $instance = null;
                self::$instance = $instance;
                return $instance;
            }
            $u = parse_config($config, '<replica_db_user>');
            $p = parse_config($config, '<replica_db_passwd>');
            $n = parse_config($config, '<replica_db_name>');
            if (($fallback_mode > 0) && (!$u || !$p || !$n)) {
                error_log("BoincDb::get_aux(): <replica_db_*> required for \$fallback_mode > 0 (giving up)");
                $instance = null;
                self::$instance = $instance;
                return $instance;
            } else {
                // use replica user if given or use normal user for $fallback_mode == 0
                if ($u) $user = $u;
                if ($p) $passwd = $p;
                if ($n) $name = $n;
            }
            // skip this block if no $replica_host is specified for $fallback_mode == 0
            if ($replica_host) {
                $retval = $instance->init_conn(
                    $user, $passwd, $replica_host, $name, true
                );
                if ($retval) {
                    // needed for places where we do direct queries
                    if (!$instance->do_query("use $name")) {
                        error_log("BoincDb::get_aux(): Couldn't select database $name on $replica_host (giving up)");
                        $instance = null;
                    }
                    self::$instance = $instance;
                    return $instance;
                } elseif ($fallback_mode == 2) {
                    // no fallback to master in this case
                    error_log("BoincDb::get_aux(): Couldn't connect to $user@$replica_host (giving up)");
                    $instance = null;
                    self::$instance = $instance;
                    return $instance;
                } else {
                    error_log("BoincDb::get_aux(): Couldn't connect to $user@$replica_host (trying $user@$host next)");
                }
            }
        }
        $retval = $instance->init_conn($user, $passwd, $host, $name, false);
        if (!$retval) {
            $instance = null;
            error_log("BoincDb::get_aux(): Couldn't connect to $user@$host (giving up)");
        } else {
            // needed for places where we do direct queries
            if (!$instance->do_query("use $name")) {
                error_log("BoincDb::get_aux(): Couldn't select database $name on $host (giving up)");
                $instance = null;
            }
        }
        self::$instance = $instance;
        return $instance;
    }

    // same, but
    // 1) check for a cached connection
    // 2) check whether the "stop_web" trigger file is present
    //
    static function get($readonly = false, $fallback_mode = 0) {
        global $generating_xml;
        if (!isset(self::$instance)) {
            if (web_stopped()) {
                if ($generating_xml) {
                    xml_error(-183, "project down for maintenance");
                } else {
                    show_page("Project down for maintenance",
                        "Please check back in a few hours."
                    );
                    exit;
                }
            }
            self::get_aux($readonly, $fallback_mode);
            if (!self::$instance) {
                if ($generating_xml) {
                    xml_error(-138, "the project's database server is down");
                } else {
                    show_page(
                        "Project is down",
                        "The project's database server is down.
                        Please check back in a few hours."
                    );
                    exit;
                }
            }
        }
        return self::$instance;
    }

    static function escape_string($string) {
        $db = self::get();
        return $db->base_escape_string(trim($string));
    }
    static function error() {
        $db = self::get();
        return $db->base_error();
    }

    // if your DB connection times out, call this, then call get() again
    //
    static function reset_connection() {
        self::$instance = null;
    }
}

class BoincUser {
    static $cache;
    static function lookup($clause) {
        $db = BoincDb::get();
        return $db->lookup('user', 'BoincUser', $clause);
    }

    static function lookup_id_nocache($id) {
        $db = BoincDb::get();
        return $db->lookup_id($id, 'user', 'BoincUser');
    }
    static function lookup_id($id) {
        if (!isset(self::$cache[$id])) {
            self::$cache[$id] = self::lookup_id_nocache($id);
        }
        return self::$cache[$id];
    }
    static function lookup_auth($auth) {
        $db = BoincDb::get();
        $auth = BoincDb::escape_string($auth);
        return self::lookup("authenticator='$auth'");
    }
    static function lookup_email_addr($email_addr) {
        $db = BoincDb::get();
        $email_addr = BoincDb::escape_string($email_addr);
        return self::lookup("email_addr='$email_addr'");
    }
    // name is not necessarily unique
    //
    static function lookup_name($name) {
        $name = BoincDb::escape_string($name);
        $users = BoincUser::enum("name='$name'");
        return $users;
    }
    static function count($clause) {
        $db = BoincDb::get();
        return $db->count('user', $clause);
    }
    static function max($field) {
        $db = BoincDb::get();
        return $db->max('user', $field);
    }
    function update($clause) {
        $db = BoincDb::get();
        return $db->update($this, 'user', $clause);
    }
    static function enum($where_clause, $order_clause=null) {
        $db = BoincDb::get();
        return $db->enum('user', 'BoincUser', $where_clause, $order_clause);
    }
    static function enum_fields($fields, $where_clause, $order_clause=null) {
        $db = BoincDb::get();
        return $db->enum_fields(
            'user', 'BoincUser', $fields, $where_clause, $order_clause
        );
    }
    static function insert($clause) {
        $db = BoincDb::get();
        $ret = $db->insert('user', $clause);
        if (!$ret) return 0;
        return $db->insert_id();
    }
    function delete() {
        $db = BoincDb::get();
        $db->delete_aux('profile', "userid=$this->id");
        return $db->delete($this, 'user');
    }
    static function sum($field) {
        $db = BoincDb::get();
        return $db->sum('user', $field);
    }
    static function percentile($field, $clause, $pct) {
        $db = BoincDb::get();
        return $db->percentile('user', $field, $clause, $pct);
    }
}

class BoincTeam {
    static $cache;
    static function insert($clause) {
        $db = BoincDb::get();
        $ret = $db->insert('team', $clause);
        if (!$ret) return 0;
        return $db->insert_id();
    }
    static function lookup_id_nocache($id) {
        $db = BoincDb::get();
        return $db->lookup_id($id, 'team', 'BoincTeam');
    }
    static function lookup_id($id) {
        if (!isset(self::$cache[$id])) {
            self::$cache[$id] = self::lookup_id_nocache($id);
        }
        return self::$cache[$id];
    }
    function update($clause) {
        $db = BoincDb::get();
        return $db->update($this, 'team', $clause);
    }
    static function enum($where_clause, $order_clause=null) {
        $db = BoincDb::get();
        return $db->enum('team', 'BoincTeam', $where_clause, $order_clause);
    }
    static function lookup($clause) {
        $db = BoincDb::get();
        return $db->lookup('team', 'BoincTeam', $clause);
    }
    static function lookup_name($name) {
        $db = BoincDb::get();
        $name = BoincDb::escape_string($name);
        return self::lookup("name='$name'");
    }
    function delete() {
        $db = BoincDb::get();
        return $db->delete($this, 'team');
    }
    static function percentile($field, $clause, $pct) {
        $db = BoincDb::get();
        return $db->percentile('team', $field, $clause, $pct);
    }
    static function max($field) {
        $db = BoincDb::get();
        return $db->max('team', $field);
    }
    static function enum_fields($fields, $where_clause, $order_clause=null) {
        $db = BoincDb::get();
        return $db->enum_fields(
            'team', 'BoincTeam', $fields, $where_clause, $order_clause
        );
    }
}

class BoincTeamDelta {
    static function insert($clause) {
        $db = BoincDb::get();
        return $db->insert('team_delta', $clause);
    }
    static function enum($where_clause) {
        $db = BoincDb::get();
        return $db->enum('team_delta', 'BoincTeamDelta', $where_clause);
    }
}

class BoincHost {
    static function lookup_id($id) {
        $db = BoincDb::get();
        return $db->lookup_id($id, 'host', 'BoincHost');
    }
    function update($clause) {
        $db = BoincDb::get();
        return $db->update($this, 'host', $clause);
    }
    function delete() {
        $db = BoincDb::get();
        return $db->delete($this, 'host');
    }
    static function enum($where_clause, $order_clause=null) {
        $db = BoincDb::get();
        return $db->enum('host', 'BoincHost', $where_clause, $order_clause);
    }
    static function enum_fields($fields, $where_clause, $order_clause=null) {
        $db = BoincDb::get();
        return $db->enum_fields(
            'host', 'BoincHost', $fields, $where_clause, $order_clause
        );
    }
    static function count($clause) {
        $db = BoincDb::get();
        return $db->count('host', $clause);
    }
    static function lookup_cpid($cpid) {
        $db = BoincDb::get();
        $cpid = BoincDb::escape_string($cpid);
        return $db->lookup('host', 'BoincHost', "host_cpid='$cpid'");
    }
    static function insert($clause) {
        $db = BoincDb::get();
        $ret = $db->insert('host', $clause);
        if (!$ret) return $ret;
        return $db->insert_id();
    }
}

class BoincResult {
    static function count($clause) {
        $db = BoincDb::get();
        return $db->count('result', $clause);
    }
    static function enum($where_clause) {
        $db = BoincDb::get();
        return $db->enum('result', 'BoincResult', $where_clause);
    }
	static function enum_fields($fields, $where_clause, $order_clause) {
        $db = BoincDb::get();
		return $db->enum_fields('result', 'BoincResult', $fields, $where_clause, $order_clause);
	}
    function update($clause) {
        $db = BoincDb::get();
        return $db->update($this, 'result', $clause);
    }
    static function update_aux($clause) {
        $db = BoincDb::get();
        return $db->update_aux('result', $clause);
    }
    static function lookup_id($id) {
        $db = BoincDb::get();
        return $db->lookup_id($id, 'result', 'BoincResult');
    }
    static function lookup_name($name) {
        $db = BoincDb::get();
        $name = BoincDb::escape_string($name);
        return $db->lookup('result', 'BoincResult', "name='$name'");
    }
    function delete() {
        $db = BoincDb::get();
        return $db->delete($this, 'result');
    }
}

class BoincWorkunit {
    static function lookup_id($id) {
        $db = BoincDb::get();
        return $db->lookup_id($id, 'workunit', 'BoincWorkunit');
    }
    static function lookup($clause) {
        $db = BoincDb::get();
        return $db->lookup('workunit', 'BoincWorkunit', $clause);
    }
    static function insert($clause) {
        $db = BoincDb::get();
        $ret = $db->insert('workunit', $clause);
        if (!$ret) return $ret;
        return $db->insert_id();
    }
    static function enum($where_clause) {
        $db = BoincDb::get();
        return $db->enum('workunit', 'BoincWorkunit', $where_clause);
    }
    function update($clause) {
        $db = BoincDb::get();
        return $db->update($this, 'workunit', $clause);
    }
    static function update_aux($clause) {
        $db = BoincDb::get();
        return $db->update_aux('workunit', $clause);
    }
    static function count($clause) {
        $db = BoincDb::get();
        return $db->count('workunit', $clause);
    }
}

class BoincApp {
    static function lookup_id($id) {
        $db = BoincDb::get();
        return $db->lookup_id($id, 'app', 'BoincApp');
    }
    static function lookup($clause) {
        $db = BoincDb::get();
        return $db->lookup('app', 'BoincApp', $clause);
    }
    static function enum($where_clause) {
        $db = BoincDb::get();
        return $db->enum('app', 'BoincApp', $where_clause);
    }
    static function insert($clause) {
        $db = BoincDb::get();
        $ret = $db->insert('app', $clause);
        if (!$ret) return $ret;
        return $db->insert_id();
    }
    function update($clause) {
        $db = BoincDb::get();
        return $db->update($this, 'app', $clause);
    }
    static function sum($field, $clause=null) {
        $db = BoincDb::get();
        return $db->sum('app', $field, $clause);
    }
}

class BoincAppVersion {
    static function enum($where_clause) {
        $db = BoincDb::get();
        return $db->enum('app_version', 'BoincAppVersion', $where_clause);
    }
    static function lookup($clause) {
        $db = BoincDb::get();
        return $db->lookup('app_version', 'BoincAppVersion', $clause);
    }
    static function lookup_id($id) {
        $db = BoincDb::get();
        return $db->lookup_id($id, 'app_version', 'BoincAppVersion');
    }
    static function insert($clause) {
        $db = BoincDb::get();
        $ret = $db->insert('app_version', $clause);
        if (!$ret) return $ret;
        return $db->insert_id();
    }
    function update($clause) {
        $db = BoincDb::get();
        return $db->update($this, 'app_version', $clause);
    }
}

class BoincProfile {
    static function lookup_fields($fields, $clause) {
        $db = BoincDb::get();
        return $db->lookup_fields('profile', 'BoincProfile', $fields, $clause);
    }
    static function lookup($clause) {
        $db = BoincDb::get();
        return $db->lookup('profile', 'BoincProfile', $clause);
    }
    static function lookup_userid($userid) {
        $db = BoincDb::get();
        return $db->lookup('profile', 'BoincProfile', 'userid='.$userid);
    }
    function update($clause) {
        $db = BoincDb::get();
        return $db->update_aux('profile', $clause.' where userid='.$this->userid);
    }
    static function update_aux($clause) {
        $db = BoincDb::get();
        return $db->update_aux('profile', $clause);
    }
    static function insert($clause) {
        $db = BoincDb::get();
        return $db->insert('profile', $clause);
    }
    static function enum($where_clause=null, $order_clause=null) {
        $db = BoincDb::get();
        return $db->enum('profile', 'BoincProfile', $where_clause, $order_clause);
    }
    static function enum_fields($fields, $where_clause=null, $order_clause=null) {
        $db = BoincDb::get();
        return $db->enum_fields('profile', 'BoincProfile', $fields, $where_clause, $order_clause);
    }
    function delete() {
        $db = BoincDb::get();
        return $db->delete_aux('profile', 'userid='.$this->userid);
    }
    static function delete_aux($clause) {
        $db = BoincDb::get();
        return $db->delete_aux('profile', $clause);
    }
}

class BoincTeamAdmin {
    static function insert($clause) {
        $db = BoincDb::get();
        return $db->insert('team_admin', $clause);
    }
    static function enum($where_clause) {
        $db = BoincDb::get();
        return $db->enum('team_admin', 'BoincTeamAdmin', $where_clause);
    }
    static function delete($clause) {
        $db = BoincDb::get();
        return $db->delete_aux('team_admin', $clause);
    }
    static function lookup($teamid, $userid) {
        $db = BoincDb::get();
        return $db->lookup('team_admin', 'BoincTeamAdmin', "teamid=$teamid and userid=$userid");
    }
}

class BoincPrivateMessage {
    static function lookup_id($id) {
        $db = BoincDb::get();
        return $db->lookup_id($id, 'private_messages', 'BoincPrivateMessage');
    }
    function update($clause) {
        $db = BoincDb::get();
        return $db->update($this, 'private_messages', $clause);
    }
    static function enum($where_clause) {
        $db = BoincDb::get();
        return $db->enum('private_messages', 'BoincPrivateMessage', $where_clause);
    }
    static function insert($clause) {
        $db = BoincDb::get();
        $ret = $db->insert('private_messages', $clause);
        if (!$ret) return $ret;
        return $db->insert_id();
    }
    static function count($clause) {
        $db = BoincDb::get();
        return $db->count('private_messages', $clause);
    }
    function delete() {
        $db = BoincDb::get();
        return $db->delete($this, 'private_messages');
    }
    static function delete_aux($clause) {
        $db = BoincDb::get();
        return $db->delete_aux('private_messages', $clause);
    }
}

class BoincPlatform {
    static function enum($where_clause) {
        $db = BoincDb::get();
        return $db->enum('platform', 'BoincPlatform', $where_clause);
    }
    static function lookup_id($id) {
        $db = BoincDb::get();
        return $db->lookup_id($id, 'platform', 'BoincPlatform');
    }
    static function lookup($clause) {
        $db = BoincDb::get();
        return $db->lookup('platform', 'BoincPlatform', $clause);
    }
    function update($clause) {
        $db = BoincDb::get();
        return $db->update($this, 'platform', $clause);
    }
    static function insert($clause) {
        $db = BoincDb::get();
        return $db->insert('platform', $clause);
    }
}

class BoincHostAppVersion {
    static function enum($where_clause) {
        $db = BoincDb::get();
        return $db->enum('host_app_version', 'BoincHostAppVersion', $where_clause);
    }
    static function lookup($host_id, $app_version_id) {
        $db = BoincDb::get();
        return $db->lookup(
            'host_app_version', 'BoincHostAppVersion',
            "host_id=$host_id and app_version_id=$app_version_id"
        );
    }
    static function update_aux($clause) {
        $db = BoincDb::get();
        return $db->update_aux('host_app_version', $clause);
    }
}

// DB utility functions

// return the "latest" app versions for a given app and platform
//
function latest_avs_app_platform($appid, $platformid) {
    $avs = BoincAppVersion::enum(
        "appid=$appid and platformid = $platformid and deprecated=0"
    );
    foreach ($avs as $av) {
        foreach ($avs as $av2) {
            if ($av->id == $av2->id) continue;
            if ($av->plan_class == $av2->plan_class && $av->version_num > $av2->version_num) {
                $av2->deprecated = 1;
            } else if ($av2->beta) {
                $av2->deprecated = 1;
            }
        }
    }
    $r = array();
    foreach ($avs as $av) {
        if (!$av->deprecated) {
            $r[] = $av;
        }
    }
    return $r;
}

// return the "latest" app versions for a given app
//
function latest_avs_app($appid) {
    $platforms = BoincPlatform::enum("");
    $r = array();
    foreach ($platforms as $p) {
        $avs = latest_avs_app_platform($appid, $p->id);
        $r = array_merge($r, $avs);
    }
    return $r;
}

class BoincBadge {
    static function enum($where_clause) {
        $db = BoincDb::get();
        return $db->enum('badge', 'BoincBadge', $where_clause);
    }
    static function insert($clause) {
        $db = BoincDb::get();
        $ret = $db->insert('badge', $clause);
        if (!$ret) return 0;
        return $db->insert_id();
    }
    function update($clause) {
        $db = BoincDb::get();
        return $db->update($this, 'badge', $clause);
    }
    static function lookup_id($id) {
        $db = BoincDb::get();
        return $db->lookup_id($id, 'badge', 'BoincBadge');
    }
    static function lookup($clause) {
        $db = BoincDb::get();
        return $db->lookup('badge', 'BoincBadge', $clause);
    }
    function delete() {
        $db = BoincDb::get();
        return $db->delete($this, 'badge');
    }
}

class BoincBadgeUser {
    static function enum($where_clause) {
        $db = BoincDb::get();
        return $db->enum('badge_user', 'BoincBadgeUser', $where_clause);
    }
    static function insert($clause) {
        $db = BoincDb::get();
        $ret = $db->insert('badge_user', $clause);
        if (!$ret) return false;
        return true;
    }
    static function lookup($clause) {
        $db = BoincDb::get();
        return $db->lookup('badge_user', 'BoincBadgeUser', $clause);
    }
    static function update($clause) {
        $db = BoincDb::get();
        return $db->update_aux('badge_user', $clause);
    }
    static function delete($clause) {
        $db = BoincDb::get();
        $db->delete_aux('badge_user', $clause);
    }
    static function count($clause) {
        $db = BoincDb::get();
        return $db->count('badge_user', $clause);
    }
}

class BoincBadgeTeam {
    static function enum($where_clause) {
        $db = BoincDb::get();
        return $db->enum('badge_team', 'BoincBadgeTeam', $where_clause);
    }
    static function insert($clause) {
        $db = BoincDb::get();
        $ret = $db->insert('badge_team', $clause);
        if (!$ret) return false;
        return true;
    }
    static function lookup($clause) {
        $db = BoincDb::get();
        return $db->lookup('badge_team', 'BoincBadgeTeam', $clause);
    }
    static function update($clause) {
        $db = BoincDb::get();
        return $db->update_aux('badge_team', $clause);
    }
    static function delete($clause) {
        $db = BoincDb::get();
        $db->delete_aux('badge_team', $clause);
    }
    static function count($clause) {
        $db = BoincDb::get();
        return $db->count('badge_team', $clause);
    }
}

class BoincCreditUser {
    static function lookup($clause) {
        $db = BoincDb::get();
        return $db->lookup('credit_user', 'BoincCreditUser', $clause);
    }
    static function enum($where_clause) {
        $db = BoincDb::get();
        return $db->enum('credit_user', 'BoincCreditUser', $where_clause);
    }
    static function sum($field, $clause) {
        $db = BoincDb::get();
        return $db->sum('credit_user', $field, $clause);
    }
    static function update($clause) {
        $db = BoincDb::get();
        return $db->update_aux('credit_user', $clause);
    }
    static function delete_user($user) {
        $db = BoincDb::get();
        $db->delete_aux('credit_user', "userid=$user->id");
    }
}

class BoincCreditTeam {
    static function lookup($clause) {
        $db = BoincDb::get();
        return $db->lookup('credit_team', 'BoincCreditTeam', $clause);
    }
    static function enum($where_clause) {
        $db = BoincDb::get();
        return $db->enum('credit_team', 'BoincCreditTeam', $where_clause);
    }
    static function sum($field, $clause) {
        $db = BoincDb::get();
        return $db->sum('credit_team', $field, $clause);
    }
    static function update($clause) {
        $db = BoincDb::get();
        return $db->update_aux('credit_team', $clause);
    }
}

// DEPRECATED: use BoincDb::escape_string where possible
// 
// apply this to any user-supplied strings used in queries
// 
function boinc_real_escape_string($x) {
    if (version_compare(phpversion(),"4.3.0")>=0) {
        return BoincDb::escape_string($x);
    } else {
        $x = str_replace("'", "\'", $x);
        $x = str_replace("\"", "\\\"", $x);
        return $x;
    }
}

?>