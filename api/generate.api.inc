<?php
/**
 * @file
 * Contains API functions related to generating common/shared data types.
 */

/**
 * Generates random numbers following a slightly skewed normal distribution
 */
function trpdata_generate_random_number($min, $max, $is_float = TRUE) {

  $sum = 0;
  $randmax = $max - $min;
  if ($randmax > 0) {
    for ($i = 0; $i < 12; $i++) {
        $sum += rand(0, $randmax) / $randmax;
    }

    $random_float = $min + ($sum * ($randmax / 10));
  }
  else {
    $random_float = $max;
  }

  if ($is_float) {
    return $random_float;
  }
  else {
    return round($random_float);
  }
}

/**
 * Generates a random string.
 */
function trpdata_generate_random_string($length = 10) {

  $characters = '0123456789ab cdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQR STUVW XYZ';
  $charactersLength = strlen($characters);

  $randomString = '';
  for ($i = 0; $i < $length; $i++) {
    $randomString .= $characters[rand(0, $charactersLength - 1)];
  }

  return $randomString;

}

/**
 * Retrieve or Create the default organism: Tripalus databasica.
 *
 * @param $options
 *   An array of options
 *     - id only: return the organism_id only rather then a full object.
 * @return
 *   The organism (Default is an object but could be a stock_id based on options.
 */
function trpdata_get_default_organism($options = array()) {

  // Set defaults.
  if (!isset($options['id only'])) $options['id only'] = FALSE;

  // Check to see if the organism already exists.
  $values = array(
    'genus' => 'Tripalus',
    'species' => 'databasica',
  );
  $organism = chado_select_record('organism',array('*'), $values);

  // If it doesn't exist then create it.
  if (!$organism) {
    $values['common_name'] = 'Tripalus databasica';
    $values['abbreviation'] = 'T. databasica';
    $values['comment'] = 'Tripalus databasica is not a real organism but used for demonstration purposes.';
    $organism = chado_insert_record('organism',$values);
  }
  else {
    $organism = $organism[0];
  }

  // Ensure we are returning an object in all cases (except failure of cource ;-)).
  if (is_array($organism)) {
    $organism = (object) $organism;
  }

  // Finally give them what they asked for.
  if ($options['id only'] AND $organism) {
    return $organism->organism_id;
  }
  return $organism;

}

/**
 * Generate stocks.
 *
 * @param $values
 *   An array of values to create the stock with. If the name and uniquename
 *   are left blank then the random generated name will be used. If not you
 *   may use the following tokens to include the random name components:
 *     - [name]: the full two part name
 *     - [first]: the first name
 *     - [last]: the last name
 *   Required:
 *     - type_id: the type of stock.
 *     - organism_id: the organism.
 *
 * @param $options
 *   An array of options describing what kind and how many stocks you
 *   want to generate. The following options are supported:
 *     - num: the number of stocks to generate (Default=1).
 *     - country: the name of the country to restrict names to.
 *     - insert: wether the stocks created should be inserted (Default=FALSE).
 *
 * @return
 *   An array of stock objects keyed by stock_id.
 */
function generate_chado_stock($values, $options) {
  $stocks = array();
  $names_used = array();

  // Set defaults.
  if (!isset($options['num'])) $options['num'] = 1;
  if (!isset($options['insert'])) $options['insert'] = FALSE;

  // Check required.
  if (!isset($values['type_id'])) return FALSE;
  if (!isset($values['organism_id'])) return FALSE;

  // Only try to find enough names 10 times.
  for ($i=0; $i<10; $i++) {

    $num_to_grab = ($options['num'] <= 500) ? $options['num'] : 500;

    // Grab a collection of names to use for stocks.
    if (isset($options['country'])) {
      $url = 'http://uinames.com/api/?gender=female&region=' . $options['country'] . '&amount=' . $num_to_grab;
    }
    else {
      $url = 'http://uinames.com/api/?gender=female&amount=' . $num_to_grab;
    }
    print "Accessing $url.\n";
    $json = file_get_contents($url);

    // Decode the JSON to an associative array.
    $names = json_decode($json);
    // Handle if we only asked for 1...
    if (isset($names->name)) $names = array($names);

    // For each potential name...
    foreach ($names as $n) {

      // Make a two part name.
      $name = $n->name . ' ' . $n->surname;
      $tokens = array(
        '[name]' => $name,
        '[first]' => $n->name,
        '[last]' => $n->surname,
      );

      // Compose the stock.
      $v = $values;
      // Set the name including tokens if applicable.
      if (isset($v['name'])) {
        $v['name'] = strtr($v['name'], $tokens);
      } else { $v['name'] = $name; }
      // Set the uniquename including tokens if applicable.
      if (isset($v['uniquename'])) {
        $v['uniquename'] = strtr($v['uniquename'], $tokens);
      } else { $v['uniquename'] = $name; }

      // Check if this stock is already in the database.
      $used = chado_select_record('stock', array('stock_id'), $v);

      // If it's not then add it to the file and keep track of the name for later.
      if (!empty($name) AND !$used and !isset($names_used[$name])) {

        if ($options['insert']) {
          $stock = chado_insert_record('stock', $v);

          if ($stock) {
            $stock = (object) $stock;
            $stocks[ $stock->stock_id ] = $stock;
            $names_used[ $name ];
          }
        }
        else {
          $stock = (object) $v;
          $names_used[ $name ];
          $stocks[] = $stock;
        }
      }

      if (sizeof($stocks) >= $options['num']) return $stocks;
    }
  }

  // If we found enough then return them, otherwise return FALSE.
  if (sizeof($stocks) >= $options['num']) return $stocks;
  return FALSE;
}

/**
 * Check database size.
 *
 * @param $pretty
 *    TRUE if you would like a human-readable size returned, FALSE otherwise.
 */
function trpdata_get_database_size($pretty = TRUE) {

  // Ask the database for it's size.
  if ($pretty) {
    $sql = 'SELECT pg_size_pretty(pg_database_size(current_database())) as fulldbsize';
  }
  else {
    $sql = 'SELECT pg_database_size(current_database()) as fulldbsize';
  }

  // And return it. Simple as that.
  return db_query($sql)->fetchField();

}
