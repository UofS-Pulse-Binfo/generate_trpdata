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
 *     - samples: An array of samples as returned from generate_nd_samples() (Default=Generate 10 samples)
 *     - out-stub: the full path stub to create copy files with (Default=/tmp/massive_genotype_dataset.drush)
 *     - load mview: whether or not to load the new genotype calls into mview_ndg_calls (Default=FALSE)
 *     - no copy: whether or not to use the COPY command to insert markers/variants (Default=FALSE)
 */
function generate_nd_genotype_calls($options) {
  $copy_chunk_size = 50000;

  // Define the defaults:
  if (!isset($options['project_id'])) return FALSE;
  if (!isset($options['alleles'])) $options['alleles'] =array('A' => 'A', 'T' => 'T', 'C' => 'C', 'G' => 'G');
  if (!isset($options['samples'])) $options['samples'] = generate_nd_samples(array('num' => 10, 'insert' => TRUE, 'generate germplasm' => TRUE));
  if (!isset($options['out_stub'])) $options['out_stub'] = '/tmp/massive_genotype_dataset.drush';
  if (!isset($options['load mview'])) $options['load mview'] = FALSE;
  if (!isset($options['no copy'])) $options['no copy'] = FALSE;

  $options['expected_total'] = chado_query('SELECT count(*) FROM {project_feature} WHERE project_id=:project',
    array(':project' => $options['project_id']))->fetchField() * sizeof($options['samples']);
  $options['observed_total'] = 0;

  $featureprop_type_id = $options['cvterm']['marker_type'];
  $variant_type_id = $options['cvterm']['SNP'];

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

  // If "no nopy" then we want to use prepared statements to speed the process
  // up as much as possible.
  if ($options['no copy']) {

    // The following is the setup needed to
    // use prepared queries with the Drupal DB API.
    // Get the Drupal database connection and change the statement class to PDOStatement.
    // Save the current class for cleanup later.
    // ASSUMPTION: the chado database is in a schema named "chado".
    $conn = Database::getConnection();
    $saved_class = $conn->getAttribute(PDO::ATTR_STATEMENT_CLASS);
    $conn->setAttribute(PDO::ATTR_STATEMENT_CLASS, array('PDOStatement'));

    // Insert statement for the genotype call.
    $insert_call = $conn->prepare(
      'INSERT INTO chado.genotype_call
        (variant_id, marker_id, genotype_id, project_id, stock_id)
        VALUES (:variant_id, :marker_id, :genotype_id, :project_id, :stock_id)');

    // Insert statement for the materialized view.
    // @todo remove hard-coding of the mview name.
    $insert_mview = $conn->prepare(
      'INSERT INTO chado.mview_ndg_tripalus_calls
        (variant_id, marker_id, marker_name, marker_type, genotype_id,
          allele_call, project_id, stock_id, stock_name, germplasm_id, germplasm_name)
        VALUES (:variant_id, :marker_id, :marker_name, :marker_type, :genotype_id,
          :allele_call, :project_id, :stock_id, :stock_name, :germplasm_id, :germplasm_name)');

  }
  // Otherwise, the default is to use the COPY command which requires saving
  // data to files before slurping them into the database in chunks.
  // Here we open the files we are going to save the data to temporarily.
  else {
    $call_file = $options['out_stub'] . '.genotype_calls.csv';
    $CALL = fopen($call_file, 'w');
    $mview_file = $options['out_stub'] . '.mview_ndg_calls.csv';
    $MVIEW = fopen($mview_file, 'w');
  }
  print " - Genotype calls...\n";

  drush_print("Memory: ".memory_get_usage());

  //=======================
  // Now grab each marker...
  $rel_type_id = $options['cvterm']['is_marker_of'];
  $args = array(':project' => $options['project_id'], ':reltype' => $rel_type_id);
  if ($options['load mview']) {
    $sql = "SELECT pf.feature_id as marker_id, m.name as marker_name, mt.value as marker_type, fr.object_id as variant_id
            FROM {project_feature} pf
            LEFT JOIN {feature_relationship} fr ON fr.subject_id=pf.feature_id AND fr.type_id=:reltype
            LEFT JOIN {feature} m ON m.feature_id=pf.feature_id
            LEFT JOIN {featureprop} mt ON mt.feature_id=m.feature_id AND mt.type_id=:prop_type
            WHERE project_id=:project";
    $args[':prop_type'] = $featureprop_type_id;
  }
  else {
    $sql = "SELECT pf.feature_id as marker_id, fr.object_id as variant_id
            FROM {project_feature} pf
            LEFT JOIN {feature_relationship} fr ON fr.subject_id=pf.feature_id AND fr.type_id=:reltype
            WHERE project_id=:project";
  }
  // Since prepared statements make us step back from the Drupal abstraction
  // we need to change how we execute all queries when using the no-copy method.
  if ($options['no copy']) {
    // Remove the {tablename}.
    $sql = preg_replace('/\{(\w+)\}/','chado.\\1', $sql);
    // Apparently you cannot pass arguements to PDO::query() and thus
    // you have to use prepared queries even if only using it once :-(.
    $resource = $conn->prepare($sql);
    $resource->setFetchMode(PDO::FETCH_OBJ);
    $resource->execute($args);

  }
  else {
    $resource = chado_query($sql, $args);
  }
  $num_markers = 0;
  drush_print("Memory (before while): ".memory_get_usage());
  while ($r = $resource->fetch()) {
    $num_markers++;

    drush_print("Memory (ea. iteration): ".memory_get_usage());

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
      $genotype_call = array(
        ':variant_id' => $r->variant_id,
        ':marker_id' => $r->marker_id,
        ':genotype_id' => $genotypes[ $call ],
        ':project_id' => $options['project_id'],
        ':stock_id' => $sample_id,
      );

      $mview = array(
        ':variant_id' => $r->variant_id,
        ':marker_id' => $r->marker_id,
        ':marker_name' => $r->marker_name,
        ':marker_type' => $r->marker_type,
        ':stock_id' => $sample_id,
        ':stock_name' => $sample['sample']->name,
        ':germplasm_id' => $sample['germplasm']->stock_id,
        ':germplasm_name' => $sample['germplasm']->name,
        ':project_id' => $options['project_id'],
        ':genotype_id' => $genotypes[ $call ],
        ':allele_call' => $call,
      );

      // Depending upon our method...
      // Either use the prepared statements if 'no copy' was specified.
      if ($options['no copy']) {

        $insert_call->execute($genotype_call);
        if ($options['load mview']) {
          $insert_mview->execute($mview);
        }

        // Report progress?
        if ($num%$copy_chunk_size == 0) {
          drush_log(' - Saved '.number_format($num).' calls so far (all samples for '.number_format($num_markers).' markers).', 'success');
        }
      }
      // Or use the copy method.
      else {

        // Save data to the appropriate files...
        fputcsv($CALL, $call);
        if ($options['load mview']) {
          fputcsv($MVIEW, $mview);
        }

        // If we have reached the size of a chunk then go ahead and copy it into the db.
        if ($num%$copy_chunk_size == 0) {
          $time_to_copy = TRUE;
        }
      }
    }

    if ($time_to_copy) {
      fclose($CALL); fclose($MVIEW);

      trpdata_remote_copy("COPY chado.genotype_call (variant_id, marker_id, genotype_id, project_id, stock_id) FROM '$call_file' WITH CSV");
      drush_log(' - Saved '.number_format($num).' calls to the genotype_call table so far (all samples for '.number_format($num_markers).' markers).', 'success');

      if ($options['load mview']) {
        trpdata_remote_copy("COPY chado.mview_ndg_tripalus_calls (variant_id, marker_id, marker_name, marker_type, stock_id, stock_name, germplasm_id, germplasm_name, project_id, genotype_id, allele_call) FROM '$mview_file' WITH CSV");
        drush_log(' - Saved '.number_format($num).' calls to the mview_ndg_calls table so far.', 'success');
      }

      $CALL = fopen($call_file, 'w'); $MVIEW = fopen($mview_file, 'w');
      $time_to_copy = FALSE;
    }

  }

  // Last of all, we need to clean-up.
  // If, 'no copy' was specified then we need to revert the connection to
  // its previous statement class.
  if ($options['no copy']) {
    $conn->setAttribute(PDO::ATTR_STATEMENT_CLASS, $saved_class);
  }
  // Otherwise, we need to close the copy files and copy in the last chunk.
  else {
    fclose($CALL); fclose($MVIEW);
    trpdata_remote_copy("COPY chado.genotype_call (variant_id, marker_id, genotype_id, project_id, stock_id) FROM '$call_file' WITH CSV");
    drush_log(' - Complete! Saved a total of '.number_format($num).' calls to the genotype_call table.', 'success');
    if ($options['load mview']) {
      trpdata_remote_copy("COPY chado.mview_ndg_tripalus_calls (variant_id, marker_id, marker_name, marker_type, stock_id, stock_name, germplasm_id, germplasm_name, project_id, genotype_id, allele_call) FROM '$mview_file' WITH CSV");
      drush_log(' - Complete! Saved a total of '.number_format($num).' calls to the mview_ndg_tripalus_calls table.', 'success');
    }
  }
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
 *     - marker_type: the free-text string describing the type of marker (Default=Cool Technology)
 *     - marker_stub: the suffix to add to variant names to make marker uniquenames (Default=[sanitized_marker_type]_[date])
 *     - load mview: whether or not to load the new variants into mview_ndg_variants (Default=FALSE)
 *     - no copy: whether or not to use the COPY command to insert markers/variants (Default=FALSE)
 */
function generate_nd_markers($options) {
  $copy_chunk_size = 50000;

  // Define the defaults.
  if (!isset($options['chr'])) return FALSE;
  if (!isset($options['project_id'])) return FALSE;
  if (!isset($options['num_per_chr'])) $options['num_per_chr'] = 1000;
  if (!isset($options['out_stub'])) $options['out_stub'] = '/tmp/massive_genotype_dataset.drush';
  if (!isset($options['organism_id'])) $options['organism_id'] = trpdata_get_default_organism(array('id only' => TRUE));
  if (!isset($options['marker_type'])) $options['marker_type'] = 'Cool Technology';
  if (!isset($options['marker_stub'])) $options['marker_stub'] = strtolower(str_replace(' ','_', $options['marker_type'])) . '_' . date('YMd');
  if (!isset($options['load mview'])) $options['load mview'] = FALSE;
  if (!isset($options['no copy'])) $options['no copy'] = FALSE;

  $options['expected_total'] = $options['num_per_chr'] * 2; // *2 because there are both markers and variants.

  // Retieve the types.
  $marker_type_id = $options['cvterm']['genetic_marker'];
  $options['marker_type_id'] = $marker_type_id;
  $variant_type_id = $options['cvterm']['SNP'];
  $options['variant_type_id'] = $variant_type_id;
  $rel_type_id = $options['cvterm']['is_marker_of'];
  $options['rel_type_id'] = $rel_type_id;
  $prop_type_id = $options['cvterm']['marker_type'];
  $options['prop_type_id'] = $prop_type_id;

  // If "no nopy" then we want to use prepared statements to speed the process
  // up as much as possible.
  if ($options['no copy']) {

    // The following is the setup needed to
    // use prepared queries with the Drupal DB API.
    // Get the Drupal database connection and change the statement class to PDOStatement.
    // Save the current class for cleanup later.
    // ASSUMPTION: the chado database is in a schema named "chado".
    $conn = Database::getConnection();
    $saved_class = $conn->getAttribute(PDO::ATTR_STATEMENT_CLASS);
    $conn->setAttribute(PDO::ATTR_STATEMENT_CLASS, array('PDOStatement'));

    // Insert statement for the variants & markers.
    $insert_feature = $conn->prepare(
      'INSERT INTO chado.feature
        (organism_id, name, uniquename, type_id)
        VALUES (:organism_id, :name, :uniquename, :type_id)');

    // Retrieve the feature_id of the last entered feature
    $get_feature_id = $conn->prepare('SELECT last_value FROM chado.feature_feature_id_seq');

    // Insert statement for project_feature.
    // Needed to attach genotype calls later!
    $insert_project = $conn->prepare(
      'INSERT INTO chado.project_feature
        (project_id, feature_id)
        VALUES (:project_id, :feature_id)');

    // Insert statement for feature properties.
    $insert_featureprop = $conn->prepare(
      'INSERT INTO chado.featureprop
        (feature_id, type_id, value)
        VALUES (:feature_id, :type_id, :value)');

    // Insert statement for feature locations.
    $insert_featureloc = $conn->prepare(
      'INSERT INTO chado.featureloc
        (srcfeature_id, feature_id, fmin, fmax)
        VALUES (:srcfeature_id, :feature_id, :fmin, :fmax)');

    // Insert statement for feature relationship.
    $insert_feature_rel = $conn->prepare(
      'INSERT INTO chado.feature_relationship
        (subject_id, type_id, object_id)
        VALUES (:marker_id, :type_id, :variant_id)');

    // Insert Statement for the materialized view.
    // @todo remove hard-coding of the mview name.
    $insert_mview = $conn->prepare(
      'INSERT INTO chado.mview_ndg_tripalus_variants
        (variant_id, variant_name, srcfeature_id, srcfeature_name, fmin, fmax)
        VALUES (:variant_id, :variant_name, :srcfeature_id, :srcfeature_name, :fmin, :fmax)');

  }
  // Otherwise, the default is to use the COPY command which requires saving
  // data to files before slurping them into the database in chunks.
  // Here we open the files we are going to save the data to temporarily.
  else {
    // COPY File for feature table.
    $feature_file = $options['out_stub'] . '.feature.csv';
    $FEATURE = fopen($feature_file, 'w');
    // COPY file for featureloc table.
    // We need two because our marker and variant feature_ids are in separate
    // files and cannot be interleaved like these locations would be.
    $featurelocV_file = $feature_file . '.variants.featureloc.csv';
    $LOCV = fopen($featurelocV_file, 'w');
    $featurelocM_file = $feature_file . '.markers.featureloc.csv';
    $LOCM = fopen($featurelocM_file, 'w');
    // COPY file for mview_ndg_variants file.
    // We only need one here since it's only the variant feature_id that we need :-).
    $mview_file = $feature_file . '.variants.mview_ndg_variants.csv';
    $MVIEW = fopen($mview_file, 'w');
  }

  // Now generate variants/markers for that chromosome.
  $options['observed_total'] = 0;
  for($num=1; $num <= $options['num_per_chr']; $num++) {

    // Determine the current position and marker name.
    $offset = rand(1,99);
    $pos = $pos + $offset;
    $variant_name = $options['chr'] . 'p' . $pos;
    $marker_name = $options['chr'] . 'p' . $pos . '_' . $options['marker_stub'];

    // Create a values array for each feature record...
    $variant = array(
      ':organism_id' => $options['organism_id'],
      ':name' => $variant_name,
      ':uniquename' => $variant_name,
      ':type_id' => $variant_type_id,
    );
    $marker = array(
      ':organism_id' => $options['organism_id'],
      ':name' => $variant_name,
      ':uniquename' => $marker_name,
      ':type_id' => $marker_type_id,
    );

    // Then Properties (Not ready to copy in at this point, need feature_ids added later).
    $variantprop = array(
      ':type_id' => $options['cvterm']['marker_type'],
      ':value' => 'SNP',
    );
    $markerprop = array(
      ':type_id' => $options['cvterm']['marker_type'],
      ':value' => $options['marker_type'],
    );

    // Now locations (Not ready to copy in at this point, need feature_ids added later).
    $variant_loc = array(
      ':srcfeature_id' => $options['chr_id'],
      ':fmin' => $pos-1,
      ':fmax' => $pos,
    );
    $marker_loc = array(
      ':srcfeature_id' => $options['chr_id'],
      ':fmin' => $pos-1,
      ':fmax' => $pos,
    );

    // Relationship between them (Not ready to copy in at this point, need variant feature_ids added later)...
    $rel = array(
      ':type_id' => $options['cvterm']['is_marker_of'],
    );

    $project = array(
      ':project_id' => $options['project_id'],
    );

    // Now mview (Not ready to copy in at this point, need variant feature_ids added later).
    $mview = array(
      ':variant_name' => $variant,
      ':srcfeature_id' => $options['chr_id'],
      ':srcfeature_name' => $options['chr'],
      ':fmin' => $pos-1,
      ':fmax' => $pos,
    );

    // Finally, depending upon our method...
    // Either use the prepared statements if 'no copy' was specified.
    if ($options['no copy']) {

      // Variant.
      $insert_feature->execute($variant);
      $get_feature_id->execute();
      $variant_id = current($get_feature_id->fetch());

      // Variant Property?
      $variantprop[':feature_id'] = $variant_id;
      $insert_featureprop->execute($variantprop);

      // Marker.
      $insert_feature->execute($marker);
      $get_feature_id->execute();
      $marker_id = current($get_feature_id->fetch());

      // Marker Propery.
      $markerprop[':feature_id'] = $marker_id;
      $insert_featureprop->execute($markerprop);

      // Locations.
      $variant_loc[':feature_id'] = $variant_id;
      $insert_featureloc->execute($variant_loc);
      $marker_loc[':feature_id'] = $marker_id;
      $insert_featureloc->execute($marker_loc);

      // Relationship.
      $rel[':variant_id'] = $variant_id;
      $rel[':marker_id'] = $marker_id;
      $insert_feature_rel->execute($rel);

      // Relationship.
      $project[':feature_id'] = $marker_id;
      $insert_project->execute($project);

      // Mview.
      if ($options['load mview']) {
        $mview[':variant_id'] = $variant_id;
        $insert_mview->execute($mview);
      }

      // Report progress?
      if ($num%$copy_chunk_size == 0) {
        drush_log("   - Generated ".number_format($num)." markers and variants.",'success');
      }

    }
    // Or use the copy method.
    else {

      // Save data to the appropriate files...
      fputcsv($FEATURE, $variant);
      fputcsv($FEATURE, $marker);
      fputcsv($LOCV, $variant_loc);
      fputcsv($LOCM, $marker_loc);

      if ($options['load mview']) {
        fputcsv($MVIEW, $mview);
      }

      // If we have reached the size of a chunk then go ahead and copy it into the db.
      if ($num%$copy_chunk_size == 0) {
        fclose($FEATURE); fclose($LOCM); fclose($LOCV); fclose($MVIEW);

        _generate_nd_markers_copy_helper($options, $feature_file);

        $FEATURE = fopen($feature_file, 'w');
        $LOCV = fopen($featurelocV_file, 'w');
        $LOCM = fopen($featurelocM_file, 'w');
        $MVIEW = fopen($mview_file, 'w');
      }
    }

  }

  // Last of all, we need to clean-up.
  // If, 'no copy' was specified then we need to revert the connection to
  // its previous statement class.
  if ($options['no copy']) {
    $conn->setAttribute(PDO::ATTR_STATEMENT_CLASS, $saved_class);
  }
  // Otherwise, we need to close the copy files and copy in the last chunk.
  else {
    fclose($FEATURE); fclose($LOCM); fclose($LOCV); fclose($MVIEW);
    _generate_nd_markers_copy_helper($options, $feature_file);
  }
}

function _generate_nd_markers_copy_helper(&$options, $feature_file) {

  // Now, copy the last chunk of data into the feature table.
  $num_lines = intval(exec("wc -l '$feature_file'"));
  if ($num_lines == 0) return TRUE;
  $options['observed_total'] = $options['observed_total'] + $num_lines;
  drush_log("   - Generated ".number_format($num_lines)." markers and variants (".number_format($options['observed_total'])." of ".number_format($options['expected_total']).")",'success');

  //$num_lines = chado_query("COPY chado.feature (organism_id, name, uniquename, type_id) FROM '$feature_file' WITH CSV")->rowCount();
  trpdata_remote_copy("COPY chado.feature (organism_id, name, uniquename, type_id) FROM '$feature_file' WITH CSV");

  //drush_log("     - Saved $num_lines markers and variants to the feature table",'success');

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
  trpdata_remote_copy("COPY chado.project_feature (feature_id, project_id) FROM '$feature_file.markers.project.csv' WITH CSV");
  trpdata_remote_copy("COPY chado.featureprop (feature_id, type_id, value) FROM '$feature_file.markers.featureprop.csv' WITH CSV");
  trpdata_remote_copy("COPY chado.featureloc (srcfeature_id, fmin, fmax, feature_id) FROM '$feature_file.markers.featureloc.processed.csv' WITH CSV");
  trpdata_remote_copy("COPY chado.featureloc (srcfeature_id, fmin, fmax, feature_id) FROM '$feature_file.variants.featureloc.processed.csv' WITH CSV");
  if ($options['load mview']) {
    trpdata_remote_copy("COPY chado.mview_ndg_tripalus_variants (variant_name, srcfeature_id, srcfeature_name, fmin, fmax, variant_id) FROM '$feature_file.variants.mview_ndg_variants.processed.csv' WITH CSV");
  }
  trpdata_remote_copy("COPY chado.feature_relationship (subject_id, object_id, type_id) FROM '$feature_file.markers.relationships.csv' WITH CSV");

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
 *     - country: the name of the contry to restrict generated names to (Default: NULL)
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
  if (!isset($options['country'])) $options['country'] = NULL;

  // Get Types.
  $sample_type_id = $options['cvterm']['DNA'];
  $germplasm_type_id = $options['cvterm']['Accession'];
  $stockrel_type_id = $options['cvterm']['is_extracted_from'];

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
