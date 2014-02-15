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
		'off' => array(0, 0, 0),
		'white' => array(220, 200, 100),
		'red' => array(255, 0, 0),
		'orange' => array(255, 127, 0),
		'yellow' => array(255, 255, 0),
		'green' => array(0, 255, 0),
		'blue' => array(0, 0, 255),
		'cyan' => array(0, 255, 255),
		'purple' => array(200, 0, 255),
		'magenta' => array(255, 255, 0),
	);
	// </config>

	assert($channelsPerLamp >= 3);

	$modes = array(
		'all',
		'random',
	);
	if($lamps >= 4) {
		$modes[] = 'grouped';
	}

	$channels = $lamps * $channelsPerLamp;
	$steps = count($c) * $lamps * 10;

	$prog = array();
	for($step = 0; $step < $steps; $step++) {
		$state = array_fill(0, $channels, '  0');

		$mode = $modes[array_rand($modes)];
		switch($mode) {
			case 'all':
				$color = $c[array_rand($c)];
				for($lamp = 0; $lamp < $lamps; $lamp++) {
					for($i = 0; $i < 3; $i++) {
						$v = round($color[$i] * $intensity[$lamp]);
						$v = sprintf('% 3d', $v);
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
							$v = sprintf('% 3d', $v);
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
						$v = sprintf('% 3d', $v);
						$state[$lamp * $channelsPerLamp + $i] = $v;
					}
				}
				break;
		}
		$prog[] = '{'. implode(', ', $state) .'}';
	}
?>
int programma_steps = <?php echo $steps; ?>;
int programma_channels = <?php echo $channels; ?>;
unsigned char programma[<?php echo $steps; ?>][<?php echo $channels; ?>] = {
	<?php echo implode(",\n\t", $prog) ."\n"; ?>
};
