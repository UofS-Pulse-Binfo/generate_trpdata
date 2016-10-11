<?php
/**
 * @file
 * Contains API functions related to generating common/shared data types.
 */

/**
 * Generate Genotype Calls (uses genotype_call table)
 *
 * @param $options
 *   An array of options describing what kinds of genotype calls you want generated (* required)
 *     * project_id: the project_id linking all markers we should generate genotype_calls for.
 *     - alleles: an array of alleles to use (Default=A,T,C,G)
 *     - samples: An array of samples as returned from generate_nd_samples() (Default=Generate 10 samples).
 *     - out-stub: the full path stub to create copy files with (Default=/tmp/massive_genotype_dataset.drush)
 */
function generate_nd_genotype_calls($options) {
  $copy_chunk_size = 500; //50000;

  // Define the defaults:
  if (!isset($options['project_id'])) return FALSE;
  if (!isset($options['alleles'])) $options['alleles'] =array('A' => 'A', 'T' => 'T', 'C' => 'C', 'G' => 'G');
  if (!isset($options['samples'])) $options['samples'] = generate_nd_samples(array('num' => 10, 'insert' => TRUE, 'generate germplasm' => TRUE));
  if (!isset($options['out_stub'])) $options['out_stub'] = '/tmp/massive_genotype_dataset.drush';

  // Open the file for copying from.
  $call_file = $options['out_stub'] . '.genotype_calls.csv';
  $CALL = fopen($call_file, 'w');
  print " - Genotype calls...\n";

  $options['expected_total'] = chado_query('SELECT count(*) FROM {project_feature} WHERE project_id=:project',
    array(':project' => $options['project_id']))->fetchField() * sizeof($options['samples']);
  $options['observed_total'] = 0;

  //--------------------
  // Genotypes
  // Save all the genotype combinations and keep the genotype_ids for later.
  // Note: Genotype_ids will be looked up if they already exist.
  $genotypes = array();
  foreach ($options['alleles'] as $A) {
    foreach ($options['alleles'] as $B) {

      $call = $A . $B;
      $values = array(
        'name' => $call,
        'uniquename' => $call,
        'description' => $call,
      );
      $r = chado_select_record('genotype',array('*'), $values);
      if (!$r) {
        $values['type_id'] = $variant_type_id;
        $r = chado_insert_record('genotype', $values);
      }
      else {
        $r = (array) $r[0];
      }
      $genotypes[$call] = $r['genotype_id'];

    }
  }
  if (sizeof($genotypes)) { drush_log(" - Ensuring all " . sizeof($genotypes).' SNP genotypes are available.', 'success'); }
  else { return drush_set_error("   - Unable to find or insert genotypes."); }

  //=======================
  // Now grab each marker...
  $sql = "SELECT pf.feature_id as marker_id, fr.object_id as variant_id
            FROM {project_feature} pf
            LEFT JOIN {feature_relationship} fr ON fr.subject_id=pf.feature_id AND fr.type_id=:reltype
            WHERE project_id=:project";
  $rel_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='is_marker_of'")->fetchField();
  $resource = chado_query($sql, array(':project' => $options['project_id'], ':reltype' => $rel_type_id));
  foreach ($resource as $r) {

    // Pick a random two alleles.
    $SNP = array_rand($options['alleles'], 2);

    $AA = $SNP[0] . $SNP[0];
    $AB = $SNP[0] . $SNP[1];
    $BB = $SNP[1] . $SNP[1];

    $frequencies = array();
    // Choose a random number to use as the frequency for the AA call.
    $frequencies[ $AA ] = rand(10,60);
    // Choose a random numberto use as the frequency for the AB call.
    $frequencies[ $AB ] = rand(0,20);
    // Then the frequency of the BB call is the remaining percent.
    $frequencies[ $BB ] = 100 - $frequencies[ $AA ] - $frequencies[ $AB ];

    // For each sample, determine the call and save it to the genotypes file.
    foreach ($options['samples'] as $sample_id => $sample) {
      $num++;

      $call = pick_random_allele($frequencies);

      //--------------------
      // genotype_call.
      fputcsv($CALL, array(
        $r->variant_id, // variant_id
        $r->marker_id, // marker_id
        $genotypes[ $call ], //genotype_id
        $options['project_id'],  // project_id
        $sample_id,  // stock_id
      ));

      // If we have reached the size of a chunk then go ahead and copy it into the db.
      if ($num%$copy_chunk_size == 0) {
        fclose($CALL);
        $num_lines = chado_query("COPY chado.genotype_call (variant_id, marker_id, genotype_id, project_id, stock_id) FROM '$call_file' WITH CSV")->rowCount();
        $options['observed_total'] = $options['observed_total'] + $num_lines;
        drush_log(" - Saved $num_lines calls to the genotype_call table (".$options['observed_total']." of ".$options['expected_total'].")",'success');
        $CALL = fopen($call_file, 'w');
      }
    }

  }

  // Copy in any remaining records.
  fclose($CALL);
  $num_lines = chado_query("COPY chado.genotype_call (variant_id, marker_id, genotype_id, project_id, stock_id) FROM '$call_file' WITH CSV")->rowCount();
  if ($num_lines == 0) return TRUE;
  drush_log(" - Saved $num_lines calls to the genotype_call table",'success');
}

/**
 * Pick a random genotype call from a list of alleles based on frequencies.
 *
 * @param $allele_frequencies
 *   An array where the keys are alleles (ie: AA) and the value is the frequency of that
 *   allele.
 * @return
 *   The random allele chosen based on frequencies (ie: AA).
 */
function pick_random_allele($allele_frequencies) {
  $rand = mt_rand(1, (int) array_sum($allele_frequencies));

  foreach ($allele_frequencies as $allele => $freq) {
    $rand -= $freq;
    if ($rand <= 0) {
      return $allele;
    }
  }
}

/**
 * Generate Markers (Type=genetic_marker) and optionally variants (Type=SNP).
 *
 * @param $options
 *   An array of options describing what kind and how many markers you
 *   want to generate. The following options are supported (* are required):
 *     - num: the number of markers to generate (Default=1)
 *     * chr: the current chromosome (ie: Chr1)
 *     * project_id: the project_id to group markers with
 *     - num-per-chr: the number of markers to generate for the current chromosome (Default=1,000)
 *     - out-stub: the full path stub to create copy files with (Default=/tmp/massive_genotype_dataset.drush)
 *     - organism_id: the organism_id to use for markers and variants (Default=Tripalus databasica)
 *     - marker_type: the free-text string describing the type of marker (Default=Exome Capture)
 *     - marker_stub: the suffix to add to variant names to make marker uniquenames (Default=exome_capture_[date])
 */
function generate_nd_markers($options) {
  $copy_chunk_size = 5; //50000;

  // Define the defaults.
  if (!isset($options['chr'])) return FALSE;
  if (!isset($options['project_id'])) return FALSE;
  if (!isset($options['num_per_chr'])) $options['num_per_chr'] = 1000;
  if (!isset($options['out_stub'])) $options['out_stub'] = '/tmp/massive_genotype_dataset.drush';
  if (!isset($options['organism_id'])) $options['organism_id'] = trpdata_get_default_organism(array('id only' => TRUE));
  if (!isset($options['marker_type'])) $options['marker_type'] = 'Exome Capture';
  if (!isset($options['marker_stub'])) $options['marker_stub'] = strtolower(str_replace(' ','_', $options['marker_type'])) . '_' . date('YMd');

  $options['expected_total'] = $options['num_per_chr'] * 2; // *2 because there are both markers and variants.

  // Retieve the types.
  $marker_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='genetic_marker'")->fetchField();
  $options['marker_type_id'] = $marker_type_id;
  $variant_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='SNP'")->fetchField();
  $options['variant_type_id'] = $variant_type_id;
  $rel_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='is_marker_of'")->fetchField();
  $options['rel_type_id'] = $rel_type_id;
  $prop_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='marker_type'")->fetchField();
  $options['prop_type_id'] = $prop_type_id;

  $feature_file = $options['out_stub'] . '.feature.csv';
  $FEATURE = fopen($feature_file, 'w');

  // Now generate variants/markers for that chromosome.
  $options['observed_total'] = 0;
  for($num=1; $num <= $options['num_per_chr']; $num++) {

    // Determine the current position and marker name.
    $offset = rand(1,99);
    $pos = $pos + $offset;
    $variant = $options['chr'] . 'p' . $pos;
    $marker = $options['chr'] . 'p' . $pos . '_' . $options['marker_stub'];

    // One line for the variant.
    fputcsv($FEATURE, array(
      $options['organism_id'], // organism_id
      $variant, // name
      $variant, // uniquename
      $variant_type_id, // type_id
    ));
    // And one line for the marker.
    fputcsv($FEATURE, array(
      $options['organism_id'], // organism_id
      $variant, // name
      $marker, // uniquename
      $marker_type_id, // type_id
    ));

    // If we have reached the size of a chunk then go ahead and copy it into the db.
    if ($num%$copy_chunk_size == 0) {
      fclose($FEATURE);
      _generate_nd_markers_copy_helper($options, $feature_file);
      $FEATURE = fopen($feature_file, 'w');
    }

  }
  fclose($FEATURE);
  _generate_nd_markers_copy_helper($options, $feature_file);
}

function _generate_nd_markers_copy_helper(&$options, $feature_file) {

  // Now, copy the last chunk of data into the feature table.
  $num_lines = intval(exec("wc -l '$feature_file'"));
  if ($num_lines == 0) return TRUE;
  $options['observed_total'] = $options['observed_total'] + $num_lines;
  drush_log("   - Generated $num_lines markers and variants (".$options['observed_total']." of ".$options['expected_total'].")",'success');

  $num_lines = chado_query("COPY chado.feature (organism_id, name, uniquename, type_id) FROM '$feature_file' WITH CSV")->rowCount();
  drush_log("     - Saved $num_lines markers and variants to the feature table",'success');

  // Using the COPY File just created, look up the markers just inserted
  // and attach them to the project.
  //-------------------------
  $markernames_file = $feature_file . '.markers';
  $variantnames_file = $feature_file . '.variants';

  // We are going to grab the marker uniquenames from the feature_file using LINUX commands
  // and save them to a file for later use.
  // First grab every other row of the feature file (ie: the lines for the markers).
  system('sed -n "0~2p" '.$feature_file.' > '.$markernames_file);
  // Then grab the other rows of the feature file (ie: the lines for the variants).
  system('sed -n "1~2p" '.$feature_file.' > '.$variantnames_file);

  // Now let our helper script pull out the feature_ids for the markers and create
  // copy files to link them to their project, variant and provide a marker type.
  $script = drupal_get_path('module','generate_trpdata') . '/includes/generate_nd_markers.helper.sh';
  system("bash $script ".implode(' ', array(
    $markernames_file,
    $options['marker_type_id'],
    $variantnames_file,
    $options['variant_type_id'],
    $options['organism_id'],
    $options['project_id'],
    $options['rel_type_id'],
    $options['prop_type_id'],
    '"' . $options['marker_type'] . '"'
  )));

  // Finally, we execute the copy commands :-).
  $num_lines = chado_query("COPY {project_feature} (feature_id, project_id) FROM '$feature_file.markers.project.csv' WITH CSV")->rowCount();
  drush_log("     - Saved $num_lines links between markers and project",'success');

  $num_lines = chado_query("COPY {featureprop} (feature_id, type_id, value) FROM '$feature_file.markers.featureprop.csv' WITH CSV")->rowCount();
  drush_log("     - Saved $num_lines marker type specifications",'success');

  $num_lines = chado_query("COPY {feature_relationship} (subject_id, object_id, type_id) FROM '$feature_file.markers.relationships.csv' WITH CSV")->rowCount();
  drush_log("     - Saved $num_lines relationships between markers and variants",'success');

}

/**
 * Generate Samples (Type=DNA) and optionally germplasm (Type=Accession).
 *
 * Three Cases:
 *   1) You want generated samples and germplasm but do not want them inserted into the db.
 *        Use $options = array('insert' => FALSE, 'generate germplasm' => TRUE)
 *   2) You want generated samplesm (not inserted) for existing germplasm
 *        Use $options = array('insert' => FALSE, 'generate germplasm' => FALSE)
 *   3) You want generated samples and germplasm with both inserted
 *        Use $options = array('insert' => TRUE, 'generate germplasm' => TRUE)
 *
 * @param $options
 *   An array of options describing what kind and how many stocks you
 *   want to generate. The following options are supported:
 *     - num: the number of stocks to generate (Default=1)
 *     - organism_id: The organism the stocks should belong to.
 *     - generate germplasm: true if you would like to generate random germplasm
 *         as well as samples (Default: FALSE)
 *     - insert: whether or not you want the stocks inserted as you go (Default: FALSE)
 *
 * @return
 *   An array keyed with sample stock_id where each item is an array with
 *   a sample and germplasm object.
 */
function generate_nd_samples($options) {
  $stocks = array();

  // Set defaults.
  if (!isset($options['num'])) $options['num'] = 1;
  if (!isset($options['organism_id'])) $options['organism_id'] = trpdata_get_default_organism(array('id only' => TRUE));
  if (!isset($options['generate germplasm'])) $options['generate germplasm'] = FALSE;
  if (!isset($options['insert'])) $options['insert'] = FALSE;

  // Get Types.
  $sample_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='DNA'")->fetchField();
  $germplasm_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='Accession'")->fetchField();
  $stockrel_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='is_extracted_from'")->fetchField();

  // Get a list of germplasm.
  // If specified, do so by generating random germplasm.
  if ($options['generate germplasm']) {
    $values = array(
      'type_id' => $germplasm_type_id,
      'organism_id' => $options['organism_id'],
    );
    $germplasm = generate_chado_stock($values, $options);
  }
  // Otherwise, simply select a random set from this organism.
  else {
    $sql = 'SELECT * FROM {stock} WHERE organism_id=:organism AND type_id != :type ORDER BY random() LIMIT ' . $options['num'];
    $germplasm = chado_query($sql, array(':organism' => $options['organism_id'], ':type' => $sample_type_id))->fetchAllAssoc('stock_id');
  }

  // Now add samples and relationships for all germplasm.
  foreach ($germplasm as $k => $germ) {

    // First the sample.
    $values = array(
      'type_id' => $sample_type_id,
      'organism_id' => $options['organism_id'],
      'name' => $germ->name . ' Extraction',
      'uniquename' => strtolower(str_replace(' ','_',$germ->name)) . '_extraction_' . date('Ymd'),
    );
    if ($options['insert']) {
      $sample = chado_insert_record('stock',$values);
      $sample = (object) $sample;
    }
    else {
      $sample = (object) $values;
    }

    // Then the relationship: sample => germplasm.
    if ($options['insert'] AND isset($sample->stock_id) AND isset($germ->stock_id)) {
      $values = array(
        'subject_id' => $sample->stock_id,
        'type_id' => $stockrel_type_id,
        'object_id' => $germ->stock_id,
      );
      chado_insert_record('stock_relationship', $values);
    }

    // Then associate with the project.
    if ($options['insert'] AND isset($sample->stock_id) AND isset($options['project_id'])) {
      $values = array(
        'stock_id' => $sample->stock_id,
        'project_id' => $options['project_id'],
      );
      chado_insert_record('project_stock', $values);
    }

    if (isset($sample->stock_id)) {
      $stocks[ $sample->stock_id ] = array(
        'sample' => $sample,
        'germplasm' => $germ
      );
    }
    else {
      $stocks[] = array(
        'sample' => $sample,
        'germplasm' => $germ
      );
    }
  }

  return $stocks;
}
