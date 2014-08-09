<?php
	// <config>
	$lamps = 4;
	$intensity = array(
		1,
		1,
		1,
		0.2,
	);
	$channelsPerLamp = 8;
	$c = array(
		#'off' => array(0, 0, 0),
		'white' => array(220, 200, 100),
		'red' => array(255, 0, 0),
		'red' => array(255, 0, 0),
		'red' => array(255, 0, 0),
		'red' => array(255, 0, 0),
		'orange' => array(255, 127, 0),
		'yellow' => array(255, 255, 0),
		'green' => array(0, 255, 0),
		'green' => array(0, 255, 0),
		'green' => array(0, 255, 0),
		'green' => array(0, 255, 0),
		'blue' => array(0, 0, 255),
		'blue' => array(0, 0, 255),
		'blue' => array(0, 0, 255),
		'blue' => array(0, 0, 255),
		'cyan' => array(0, 255, 255),
		'purple' => array(200, 0, 255),
		'magenta' => array(255, 255, 0),
	);
	// </config>

	assert($channelsPerLamp >= 3);

	$modes = array(
		'all',
		#'random',
		'changeone',
		'changetwo',
		'flashone',
		'flashtwo',
		'changeone',
		'changetwo',
		'flashone',
		'flashtwo',
		'flipblack',
		'flipblack',
		'flipblack',
		'flipblack',
		'flipblack',
		'flipblack',
		'flipblack',
		'flipblack',
	);
	if($lamps >= 4) {
		$modes[] = 'grouped';
	}

	$channels = $lamps * $channelsPerLamp;
	$steps = count($c) * $lamps * 10;
	$prevState = array_fill(0, $channels, '  0');

	$prog = array();
	for($step = 0; $step < $steps; $step++) {
		$state = array_fill(0, $channels, 0);

		$mode = $modes[array_rand($modes)];
		switch($mode) {
			case 'all':
				$color = $c[array_rand($c)];
				for($lamp = 0; $lamp < $lamps; $lamp++) {
					for($i = 0; $i < 3; $i++) {
						$v = round($color[$i] * $intensity[$lamp]);
						$state[$lamp * $channelsPerLamp + $i] = $v;
					}
				}
				break;
			case 'grouped':
				$l = range(0, $lamps - 1);
				shuffle($l);
				foreach(array_chunk($l, round(sqrt($lamps))) as $group) {
					$color = $c[array_rand($c)];
					foreach($group as $lamp) {
						for($i = 0; $i < 3; $i++) {
							$v = round($color[$i] * $intensity[$lamp]);
							$state[$lamp * $channelsPerLamp + $i] = $v;
						}
					}
				}
				break;
			case 'random':
				for($lamp = 0; $lamp < $lamps; $lamp++) {
					$color = $c[array_rand($c)];
					for($i = 0; $i < 3; $i++) {
						$v = round($color[$i] * $intensity[$lamp]);
						$state[$lamp * $channelsPerLamp + $i] = $v;
					}
				}
				break;
			case 'changeone':
			case 'changetwo':
			case 'flashone':
			case 'flashtwo':
				if(strncmp($mode, 'change', 6) == 0) {
					$state = $prevState;
				}
				for($times = substr($mode, -3) == 'one' ? 1 : 2; $times; $times--) {
					$lamp = mt_rand(0, $lamps-1);
					$color = $c[array_rand($c)];
					for($i = 0; $i < 3; $i++) {
						$v = round($color[$i] * $intensity[$lamp]);
						$state[$lamp * $channelsPerLamp + $i] = $v;
					}
				}
				break;
			case 'flipblack':
				$state = $prevState;
				$change = array_merge(array_fill(0, $lamps / 2, false), array_fill(0, $lamps / 2, true));
				shuffle($change);
				for($lamp = 0; $lamp < $lamps; $lamp++) {
					if($change[$lamp]) {
						$wasBlack = (array_slice($prevState, $lamp * $channelsPerLamp, 3) == array(0, 0, 0));
						$color = $wasBlack ? $c[array_rand($c)] : array(0, 0, 0);
						for($i = 0; $i < 3; $i++) {
							$v = round($color[$i] * $intensity[$lamp]);
							$state[$lamp * $channelsPerLamp + $i] = $v;
						}
					}
				}
				break;
		}
		$prog[] = '{'. implode(', ', array_map(function($v) { return sprintf('% 3d', $v); }, $state)) .'}';
		$prevState = $state;
	}
?>
int programma_steps = <?php echo $steps; ?>;
int programma_channels = <?php echo $channels; ?>;
unsigned char programma[<?php echo $steps; ?>][<?php echo $channels; ?>] = {
	<?php echo implode(",\n\t", $prog) ."\n"; ?>
};
