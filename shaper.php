
/*

Author: Andrey Sergienko <andrey.sergienko@gmail.com>
reference: www.erazer.org

*/


<?php

date_default_timezone_set("Europe/Kiev");

require_once "lib/misc.php";
require_once "lib/pid.php";
require_once "lib/log.php";
require_once "lib/net.php";

error_reporting(0);

function make_select_networks()
{
    $select = "";
    if (FALSE === ($nets = file('networks',FILE_IGNORE_NEW_LINES)))
    {
	    return "ip IS NOT NULL AND ";
    }

    foreach ($nets as $net)
    {
	    if ($net == "") { continue; };
	    $select .= "(inet_aton(ip)&inet_aton('".get_mask($net)."'))=inet_aton('".get_net($net)."') OR ";
    }
    
    $select = substr($select,0,strlen($select)-3);

    return $select;
};

$pid = new pid("pid");
if ($pid->already_running)
{
    writelog(2,"script is already running - check pid-file");
    exit;
};

$stat_ips = 0;
$speeds = array();
$stat_speed = 0;

$f_classes = fopen(F_CLASSES,"w");
if (!$f_classes) { writelog(-1,"Error when open classes"); exit; };

$f_prefixes = fopen(F_PREFIXES,"w");
if (!$f_prefixes) { writelog(-1,"Error when open prefixes"); exit; };

$db = mysql_connect(MYSQL_HOST, MYSQL_USER, MYSQL_PASSWORD);
if ($db == 0) { writelog(-1,"Error connectiong to MySQL"); exit; };

writelog(2,"*** STARTED ***");

mysql_select_db(MYSQL_DB,$db);

$dir = ("out" == DEV) ? "sout" : "sin";

// you need to have proper MYSQL tables with info about ID and IP of a client
// or you can get it here in any another comfortable way

$query = "SELECT shapers.id,ip,".$dir." FROM status,shapers WHERE status=3 AND (".make_select_networks().") AND status.shaper_id = shapers.shaper_id;";

$res = mysql_query($query);

$stat_ips = mysql_num_rows($res);

$a_shapers = array();
while (list($sid,$sip,$speed) = mysql_fetch_array($res)) { $a_shapers[$sip] = $sid.":".$speed; }

$_header = "
qdisc add dev ".DEV." root handle 1: htb r2q 50
class add dev ".DEV." parent 1: classid 1:ffff htb rate 9000Mbit ceil 9050Mbit quantum 3000
# default
# class add dev ".DEV." parent 1:1 classid 1:ffff htb rate 9000Mbit ceil 9000Mbit
# qdisc add dev ".DEV." parent 1:ffff handle ffff: sfq quantum 3000 perturb 10\n\n";
fwrite($f_classes,$_header);

$classes = array();
$qdiscs = array();
    
    foreach ($a_shapers as $k => $v)
    {    
        list($id,$speed) = explode(":",$v);
        $ip = $k;
	      $id = dechex($id);
	
        if ($speed == 0) { $speed = 32; };
	
	      $classes[$id] = "class add dev ".DEV." parent 1:ffff classid 1:".$id." htb rate ".$speed."kbit ceil ".$speed."kbit quantum 3000\n";
	      $speeds[$id] = $speed;
	
	      fwrite($f_prefixes,"$ip/32 1:$id\n");
    }
    foreach ($classes as $key => $value) { fwrite($f_classes,$value); };

    $f_speeds = fopen(F_SPEEDS,"w");
    if ($f_speeds)
    {
	    foreach ($speeds as $key => $value) { $stat_speed += $value; fwrite($f_speeds,$value." - ".$key."\n"); };
	    fclose($f_speeds);
    };

mysql_close($db);                                                                                                                                    

fclose($f_prefixes);
fclose($f_classes);

$dir = ("out" == DIR) ? "src" : "dst";

command("./prefixtree ".F_PREFIXES." ".F_FILTERS." ".DEV." ".$dir." batch");
command("/bin/cat ".F_FILTERS." >> ".F_CLASSES);

command("/bin/sort ".F_SPEEDS." -o ".F_SPEEDS);

command("/sbin/tc qdisc del dev ".DEV." root");
command("/sbin/tc -b ".F_CLASSES);

writelog(2,"Total IP's: ".$stat_ips);
writelog(2,"Total classes: ".count($classes));
writelog(2,"Total speed: ".$stat_speed." kbit");

writelog(2,"*** STOPPED ***\n");
?>
