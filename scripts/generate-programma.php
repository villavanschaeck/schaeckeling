<?php
	// <config>
	$lamps = 4;
	$intensity = array(
		'100%',
		'100%',
		'100%',
		'20%',
	);
	$channelsPerLamp = 8;
	$c = array(
		'black',
		'red',
		'orange',
		'yellow',
		'green',
		'turqoise',
		'blue',
		'lila',
		'pink',
		'magenta',
	);
	// </config>

	assert($channelsPerLamp >= 3);

	$header = array();
	for($lamp = 0; $lamp < $lamps; $lamp++) {
		$header[] = 'Kleur '. ($lamp+1);
		$header[] = 'Intensiteit '. ($lamp+1);
	}
	echo "!author;generate-programma.php\n";
	echo implode(';', $header) ."\n";

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
		$stepColors = array_fill(0, $lamps, '');
		$stepIntensities = $intensity;

		$mode = $modes[array_rand($modes)];
		switch($mode) {
			case 'all':
				$color = $c[array_rand($c)];
				$stepColors = array_fill(0, $lamps, $color);
				break;
			case 'grouped':
				$l = range(0, $lamps - 1);
				shuffle($l);
				foreach(array_chunk($l, round(sqrt($lamps))) as $group) {
					$color = $c[array_rand($c)];
					foreach($group as $lamp) {
						$stepColors[$lamp] = $color;
					}
				}
				break;
			case 'random':
				for($lamp = 0; $lamp < $lamps; $lamp++) {
					$color = $c[array_rand($c)];
					$stepColors[$lamp] = $color;
				}
				break;
		}
		$line = array_fill(0, $lamps * 2, '');
		for($lamp = 0; $lamp < $lamps; $lamp++) {
			$line[$lamp * 2] = $stepColors[$lamp];
			$line[$lamp * 2 + 1] = $stepIntensities[$lamp];
		}
		echo implode(';', $line) ."\n";
	}
?>
