<?php
	$author = 'Unknown';
	$spb = 1;
	$empty = 'off';
	$header = array();
	$previous = array();
	$steps = array();
	$useChannels = 0;

	$fh = STDIN;
	$line = 1;
	while(!feof($fh) && ($data = fgetcsv($fh, 0, ';', '"')) !== FALSE) {
		$line++;
		if($data === array(NULL)) {
			continue;
		}
		if(!empty($data[0]) && $data[0][0] == '#') {
			continue;
		} elseif(!empty($data[0]) && $data[0][0] == '!') {
			switch($data[0]) {
				case '!spb':
					if(empty($data[1]) || !preg_match('@^(1/)?(\d+)$@', $data[1], $m)) {
						echo "Error: Line ". $line .": Steps Per Beat is not a number\n";
						exit(1);
					}
					$spb = intval(($m[1] == '1/') ? -$m[2] : $m[1]);
					break;
				case '!author':
					if(empty($data[1])) {
						echo "Error: Line ". $line .": Author is empty\n";
						exit(1);
					}
					$author = $data[1];
					break;
				case '!empty':
					if(empty($data[1]) || !in_array(strtolower($data[1]), array('off', 'previous'))) {
						echo "Error: Line ". $line .": Empty is invalid. Should be 'off' or 'previous'\n";
						exit(1);
					}
					$empty = strtolower($data[1]);
					break;
				case '!label':
					// XXX
				case '!repeat':
					// XXX
				default:
					echo "Error: Line ". $line .": Invalid macro ". $data[0] ."\n";
					exit(1);
			}
			continue;
		}
		if(!$header) {
			if(array_sum(array_map('strlen', $data)) == 0) {
				continue;
			}
			foreach($data as $n => $col) {
				$header[$n] = array('name' => $col);
				if(preg_match('/^Kleur\s*(\d+)/i', $col, $m)) {
					$header[$n]['actuator'] = 'ledpar';
					$header[$n]['type'] = 'color';
					$header[$n]['channel'] = 1+($m[1]-1)*8;
					$header[$n]['channels'] = 8;
				} elseif(preg_match('/^Intensiteit\s*(\d+)/i', $col, $m)) {
					$header[$n]['actuator'] = 'ledpar';
					$header[$n]['type'] = 'intensity';
					$header[$n]['channel'] = 1+($m[1]-1)*8;
					$header[$n]['channels'] = 8;
				} else {
					echo "Error: Line ". $line .": Invalid column ". $col ."\n";
					exit(1);
				}
				$useChannels = max($useChannels, $header[$n]['channel'] + $header[$n]['channels'] - 1);
			}
			continue;
		}
		if(count($data) > count($header)) {
			echo "Error: Line ". $line .": Too many columns (". count($data) ." with only ". count($header) ." in the header)\n";
			exit(1);
		}
		$actuators = array();
		foreach($header as $col) {
			$actuators[$col['channel']] = array('actuator' => $col['actuator'], 'channel' => $col['channel']);
		}
		foreach($data as $n => &$cell) {
			assert(isset($header[$n]));
			$col = $header[$n];
			if(!$cell && $empty == 'previous') {
				if(!$previous) {
					echo "Error: Line ". $line .": Invalid empty value for ". $col['name'] ." (no previous value yet)\n";
					exit(1);
				}
				$cell = $previous[$n];
			}
			try {
				switch($col['type']) {
					case 'color':
						if(!$cell) {
							$cell = 'black';
						}
						$actuators[$col['channel']][$col['type']] = parse_color($cell);
						break;
					case 'intensity':
						if(!$cell) {
							$cell = '0';
						}
						$actuators[$col['channel']][$col['type']] = parse_intensity($cell);
						break;
				}
			} catch(Exception $e) {
				echo "Error: Line ". $line .": Invalid value ". $cell ." for ". $col['name'] ."\n";
				exit(1);
			}
		}
		$step = array_fill(0, $useChannels, 0);
		foreach($actuators as $channel => $info) {
			switch($info['actuator']) {
				case 'ledpar':
					// XXX default color and intensity
					$rgb = apply_intensity($info['color'], $info['intensity']);
					array_splice($step, $info['channel']-1, 3, $rgb);
					break;
				default:
					assert(!"reached");
			}
		}
		unset($col);
		$steps[] = $step;
		$previous = $data;
	}
	if(!feof($fh)) {
		print("Error on line ". $line ."\n");
		exit(1);
	}
	fclose($fh);

	$out = 'PN'. format_number_as_two_bytes($useChannels) . format_number_as_two_bytes(count($steps));
	foreach($steps as $n => $step) {
		$out .= 'PS'. format_number_as_two_bytes($n) . implode('', array_map('chr', $step));
	}
	$out .= 'PA';
	echo $out;

	function format_number_as_two_bytes($nr) {
		return chr(floor($nr / 256)) . chr($nr % 256);
	}

	function parse_color($color) {
		static $colors = array(
			'red'            => array(255,   0,   0),
			'orange'         => array(255,  50,   0),
			'yellow'         => array(255, 150,   0),
			'lightgreen'     => array(100, 255,   0),
			'green'          => array( 40, 255,   0),
			'darkgreen'      => array(  0, 255,   0),
			'turqoise'       => array(  0, 255,  50),
			'lightturqoise'  => array(  0, 255, 150),
			'lightblue'      => array(  0, 150, 255),
			'blue'           => array(  0,  50, 255),
			'darkblue'       => array(  0,   0, 255),
			'lila'           => array( 50,   0, 255),
			'lightpink'      => array(150,   0, 255),
			'pink'           => array(255,   0, 255),
			'magenta'        => array(220,   0,  50),
			'white'          => array(220, 200, 100),
			'black'          => array(  0,   0,   0),
		);
		if(preg_match('/^[0-9a-f]{6}$/', $color)) {
			$rgb = array_map('hexdec', str_split($color, 2));
		} elseif(isset($colors[trim($color)])) {
			$rgb = $colors[trim($color)];
		} else {
			throw new Exception('Invalid color: '. $color);
		}
		return $rgb;
	}

	function parse_intensity($intensity) {
		if(preg_match('/^(\d{1,3})%$/', $intensity, $m)) {
			$intensity = $m[1] * 255 / 100;
		} elseif(preg_match('/^(\d{1,3})$/', $intensity, $m)) {
			$intensity = $m[1];
		} else {
			throw new Exception('Invalid intensity: '. $intensity);
		}
		if($intensity < 0 || $intensity > 255) {
			throw new Exception('Invalid intensity: '. $intensity);
		}
		return $intensity;
	}

	function apply_intensity($rgb, $intensity) {
		foreach($rgb as &$c) {
			$c = round($c * $intensity / 255);
		}
		unset($c);
		return $rgb;
	}
?>
