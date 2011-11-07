<?php

/**
 * Registers the order line item with salesforce
 * @param salesforce interface $sf
 * @param object of each order product from $order $order_product
 */
function _uc_ascent_salesforce_salesforce_enter_order_line($sf, $soName, $order_product) {
  //addSOLine(string soName,string itemName,double price, double quantity,double discount)
  $id = _uc_ascent_query_table($sf, 'PBSI__PBSI_item__c', $order_product->model );
	if(!$id) {
		return false;
	}

  $discount = 0.00; //@todo integrate discounts when vouchers are in use
  
  if ($result = $sf->addSalesOrderLineItem($soName, $id , $order_product->price, $order_product->qty, $discount)) {
    return $result;
  }
  
  
  return false;
}

function _uc_ascent_salesforce_salesforce_enter($sf, $order, $sf_account_id, $sf_contact_id) {
  
  $contactName = $order->delivery_first_name . ' ' . $order->delivery_last_name;
  $shippingInstructions = _uc_ascent_get_all_order_comments($order->order_id);

  // SalesTax per Teresa's rules
  // Items are sales tax free, except in australia
  // where the shipping amount attracts a 10% tax
  $shipping_items = uc_order_load_line_items($order, true);
  $shipping_total = 0.00;
  $salesTax = 0.00;
  
  foreach($shipping_items as $item) {
    if($item['type'] == 'shipping') {
      $shipping_total += $item['amount'];
    }
  }
    
  if($order->currency == 'AUD') {
    // shipping total includes 10% of the tax, so take that out and adjust
    $salesTax = $shipping_total*0.10;
    $shipping_total = ($shipping_total - $salesTax);
  }

  $discount = 0; // @todo integrate with vouchers when that is complete	

  if ($result = $sf->addSalesOrder( $sf_account_id, 
                                    $sf_contact_id, 
                                    $shippingInstructions, 
                                    $shipping_total, 
                                    $salesTax, 
                                    $discount,
                                    $order->order_id)) {
    return $result;
  }
  return false;
}

function _uc_ascent_get_all_order_comments($order_id) {
  $result = db_query("SELECT message FROM {uc_order_comments} WHERE order_id = %d ORDER BY created DESC", $order_id);
  $output = "";
  
  while ($row = db_fetch_array($result)) {
    $output .= $row ['message'] . "\n";
  }
  
  return $output;

}
function _uc_ascent_get_order_total($order_id) {
  return db_result(db_query("SELECT sum(amount) FROM {uc_order_line_items} WHERE order_id =%d", $order_id));
}

function _uc_ascent_query_table($sf, $key, $value) {
	static $ids;	
	
	if($ids[$key.$value]) {
		return $ids[$key.$value];
	}
	
  if ( $result = $sf->queryAll("Select Id from {$key} where Name = '{$value}'") ) {
    if(count($result->records) == 0) {
	     uc_ascent_report_error($sf, 'Query SalesForce table', "$value does not exist in SalesForce/Ascent table $key");
    }
  	$ids[$key.$value] = $result->records[0]->Id;
  } 

  return $ids[$key.$value];
	
}
/**
 * Create the entry in salesforce, check to see if theres a contact by this name, 
 * if not, create an account and a contact (linked to this account)
 * @param unknown_type $sf
 * @param unknown_type $order
 * @return the contact ID of this person in SF
 */
function _uc_ascent_salesforce_account_upsert_contact_from_order($sf, $order) {
 
  if (! $order->uid) {
    // only orders that have created accounts (Should be in state payment_received)
    return false;
  }
  // make sure we escape strings in this query, these don't get automatically escaped
  $delivery_first_name = addslashes($order->delivery_first_name);
  $delivery_last_name = addslashes($order->delivery_last_name);
  
  $result = $sf->queryAll("SELECT AccountId, Id, FirstName, LastName FROM Contact WHERE 
                            FirstName = '{$delivery_first_name}' 
                            AND LastName = '{$delivery_last_name}'
                            AND Email = '{$order->primary_email}'");
  
  if ($result->size == 0) {
    $delivery_zone = uc_get_zone_code($order->delivery_zone);
    $delivery_country = uc_get_country_data(array (
      'country_id' => $order->delivery_country 
    ));
    $billing_zone = uc_get_zone_code($order->billing_zone);
    $billing_country = uc_get_country_data(array (
      'country_id' => $order->billing_country 
    ));
    
    // create the Account for this
    $record = array (
      'BillingCity' => $order->billing_city, 
      'BillingCountry' => $billing_country [0] ['country_name'], 
      'BillingPostalCode' => $order->billing_postal_code, 
      'BillingState' => $billing_zone, 
      'BillingStreet' => $order->billing_street1 . ' ' . $order->billing_street2, 
      'Description' => "Added from purchase on ChiaCo Website, order number {$order->order_id}", 
      'Name' => ucfirst(strtolower($order->billing_first_name)).' '.ucfirst(strtolower($order->billing_last_name)),    
      'Phone' => $order->billing_phone, 
      'ShippingCity' => $order->billing_city, 
      'ShippingState' => $delivery_zone, 
      'ShippingCountry' => $delivery_country [0] ['country_name'], 
      'ShippingPostalCode' => $order->delivery_postal_code, 
      'ShippingStreet' => $order->delivery_street1 . ' ' . $order->delivery_street2,
    
      // Extra custom fields
      'Type' => 'Customer',
      'Sub_Type__c' => 'Consumer Direct:Online',
      'PBSI__Payment_Terms__c' => 'Cash',
      'Current_Payment_Terms__c' => 'Cash',
      // @note: query the ens: type to lookup values for these things
      // <element name="c2g__CODAAccountsReceivableControl__r" nillable="true" minOccurs="0" type="ens:c2g__codaGeneralLedgerAccount__c"/>    
      // 'c2g__CODAAccountsReceivableControl__c' => 'Accounts Receivable', //NOT CONFIGURED
      // 'c2g__CODAAccountsPayableControl__c' => 'Accounts Payable',  //NOT CONFIGURED
      'c2g__CODAVATStatus__c' => 'Home',
      'c2g__CODADimension1__c' => _uc_ascent_query_table($sf, "c2g__CODADimension1__c", "Consumer Products"),
      'c2g__CODASalesTaxStatus__c' => 'Exempt from Qualifying Products',
      'c2g__CODATaxCode1__c' => _uc_ascent_query_table($sf, "c2g__codaTaxCode__c", "FRE"),
      'c2g__CODATaxCalculationMethod__c' => 'Gross',
      'c2g__CODAInputVATCode__c' =>  _uc_ascent_query_table($sf, "c2g__codaTaxCode__c", "FRE"),
      'c2g__CODAOutputVATCode__c' => _uc_ascent_query_table($sf, "c2g__codaTaxCode__c", "FRE"),
    );
    
    $response = $sf->create(array (
      $record 
    ), 'Account');
    
    // print_r($response);
    
    if(! $return['accountId'] = $response[0]->id) {
      uc_ascent_report_error($sf, 'Creating account failed', "Creeating account for {$order->order_id} failed");
      return false;      
    }
    
    $record = array (
      'Description' => "Added from purchase on ChiaCo Website, order number {$order->order_id}", 
      'Email' => $order->primary_email, 
      'Firstname' => ucfirst(strtolower($order->delivery_first_name)), 
      'Homephone' => $order->delivery_phone, 
      'Lastname' => ucfirst(strtolower($order->delivery_last_name)), 
      'MailingCity' => $order->delivery_city, 
      'MailingState' => $delivery_zone, 
      'MailingCountry' => $delivery_country [0] ['country_name'], 
      'MailingPostalCode' => $order->delivery_postal_code, 
      'MailingStreet' => $order->delivery_street1 . ' ' . $order->delivery_street2, 
      'MobilePhone' => $order->delivery_phone, 
      'AccountId' => $response [0]->id,
      'Customer_Type__c' => 'Consumer Direct: Online',
    );
    
    $response = $sf->create(array (
      $record 
    ), 'Contact');
    $return ['contactId'] = $response [0]->id;
  
  }
  else {
    $return ['accountId'] = $result->records [0]->AccountId;
    $return ['contactId'] = $result->records [0]->Id;
  
  }
  
  return $return;
}

class AscentSFClient extends SforceBaseClient {
  const ENTERPRISE_NAMESPACE = 'urn:enterprise.soap.sforce.com';
  public $sforce; // like the stock version but public so we can access more debug
  
  function AscentSFClient() {
    $this->namespace = self::ENTERPRISE_NAMESPACE;
  }
  
  function createAscentConnection($ascent_wsdl, $proxy = null) {
    
    $soapClientArray = array (
      'user_agent' => 'salesforce-toolkit-php/' . $this->version, 'encoding' => 'utf-8', 'trace' => 1, 'compression' => SOAP_COMPRESSION_ACCEPT | SOAP_COMPRESSION_GZIP 
    );
    
    if ($proxy != null) {
      $proxySettings = array ();
      $proxySettings ['proxy_host'] = $proxy->host;
      $proxySettings ['proxy_port'] = $proxy->port; // Use an integer, not a string
      $proxySettings ['proxy_login'] = $proxy->login;
      $proxySettings ['proxy_password'] = $proxy->password;
      $soapClientArray = array_merge($soapClientArray, $proxySettings);
    }
    
    $this->ascent = new SoapClient($ascent_wsdl, $soapClientArray);
  }
  
  /**
   * Adds one or more new individual objects to your organization's data.
   * @param array $sObjects    Array of one or more sObjects (up to 200) to create.
   * @param AssignmentRuleHeader $assignment_header is optional.  Defaults to NULL
   * @param MruHeader $mru_header is optional.  Defaults to NULL
   * @return SaveResult
   */
  public function create($sObjects, $type) {
    foreach ($sObjects as &$sObject) {
      // FIX for fieldsToNull issue - allow array in fieldsToNull (STEP #1)
      $xmlStr = '';
      if (isset($sObject->fieldsToNull) && is_array($sObject->fieldsToNull)) {
        foreach ($sObject->fieldsToNull as $fieldToNull) {
          $xmlStr .= '<fieldsToNull>' . $fieldToNull . '</fieldsToNull>';
        }
      }
      // ------
      

      $sObject = new SoapVar($sObject, SOAP_ENC_OBJECT, $type, $this->namespace);
      
      // FIX for fieldsToNull issue - allow array in fieldsToNull (STEP #2)
      if ($xmlStr != '') {
        $sObject->enc_value->fieldsToNull = new SoapVar(new SoapVar($xmlStr, XSD_ANYXML), SOAP_ENC_ARRAY);
      }
      // ------
    }
    $arg = $sObjects;
    
    return parent::_create(new SoapParam($arg, "sObjects"));
  }
  
  /**
   * Updates one or more new individual objects to your organization's data.
   * @param array sObjects    Array of sObjects
   * @param AssignmentRuleHeader $assignment_header is optional.  Defaults to NULL
   * @param MruHeader $mru_header is optional.  Defaults to NULL
   * @return UpdateResult
   */
  public function update($sObjects, $type, $assignment_header = NULL, $mru_header = NULL) {
    
    foreach ($sObjects as &$sObject) {
      
      // FIX for fieldsToNull issue - allow array in fieldsToNull (STEP #1)
      $xmlStr = '';
      if (isset($sObject->fieldsToNull) && is_array($sObject->fieldsToNull)) {
        foreach ($sObject->fieldsToNull as $fieldToNull) {
          $xmlStr .= '<fieldsToNull>' . $fieldToNull . '</fieldsToNull>';
        }
      }
      // ------
      

      $sObject = new SoapVar($sObject, SOAP_ENC_OBJECT, $type, $this->namespace);
      
      // FIX for fieldsToNull issue - allow array in fieldsToNull (STEP #2)
      if ($xmlStr != '') {
        $sObject->enc_value->fieldsToNull = new SoapVar(new SoapVar($xmlStr, XSD_ANYXML), SOAP_ENC_ARRAY);
      }
      // ------
    }
    $arg = new stdClass();
    $arg->sObjects = $sObjects;
    return parent::_update($arg);
  }
  
  /**
   * Creates new objects and updates existing objects; uses a custom field to
   * determine the presence of existing objects. In most cases, we recommend
   * that you use upsert instead of create because upsert is idempotent.
   * Available in the API version 7.0 and later.
   *
   * @param string $ext_Id External Id
   * @param array  $sObjects Array of sObjects
   * @param string $type The type of objects being upserted.
   * @return UpsertResult
   */
  public function upsert($ext_Id, $sObjects, $type = 'Contact') {
    $arg = new stdClass();
    $arg->externalIDFieldName = new SoapVar($ext_Id, XSD_STRING, 'string', 'http://www.w3.org/2001/XMLSchema');
    foreach ($sObjects as &$sObject) {
      // FIX for fieldsToNull issue - allow array in fieldsToNull (STEP #1)
      $xmlStr = '';
      if (isset($sObject->fieldsToNull) && is_array($sObject->fieldsToNull)) {
        foreach ($sObject->fieldsToNull as $fieldToNull) {
          $xmlStr .= '<fieldsToNull>' . $fieldToNull . '</fieldsToNull>';
        }
      }
      // ------
      

      $sObject = new SoapVar($sObject, SOAP_ENC_OBJECT, $type, $this->namespace);
      
      // FIX for fieldsToNull issue - allow array in fieldsToNull (STEP #2)
      if ($xmlStr != '') {
        $sObject->enc_value->fieldsToNull = new SoapVar(new SoapVar($xmlStr, XSD_ANYXML), SOAP_ENC_ARRAY);
      }
      // ------
    }
    $arg->sObjects = $sObjects;
    return parent::_upsert($arg);
  }
  
  /**
   * Merge records
   *
   * @param stdclass $mergeRequest
   * @param String $type
   * @return unknown
   */
  public function merge($mergeRequest, $type) {
    $mergeRequest->masterRecord = new SoapVar($mergeRequest->masterRecord, SOAP_ENC_OBJECT, $type, $this->namespace);
    $arg = new stdClass();
    $arg->request = new SoapVar($mergeRequest, SOAP_ENC_OBJECT, 'MergeRequest', $this->namespace);
    return parent::_merge($arg);
  }
  
  /**
   * <xsd:element name="addSOLine">
   * <xsd:complexType>
   * <xsd:sequence>
   * <xsd:element name="soId" type="xsd:string" nillable="true"/>
   * <xsd:element name="itemId" type="xsd:string" nillable="true"/>
   * <xsd:element name="price" type="xsd:double" nillable="true"/>
   * <xsd:element name="quantity" type="xsd:double" nillable="true"/>
   * <xsd:element name="discount" type="xsd:double" nillable="true"/>
   * </xsd:sequence>
   * </xsd:complexType>
   * </xsd:element>
   * 
   */
  
  public function addSalesOrderLineItem($soId, $itemId, $price, $quantity, $discount) {
    $this->_setSessionHeader($this->ascent);
    
    try {
      
      $values = array (
        'soId' => $soId, 
        'itemId' => $itemId, 
        'price' => $price, //@todo each is it?
        'quantity' => $quantity, 
        'discount' => $discount 
      );
      
      $result = $this->ascent->addSOLine($values);
    
    }
    catch (SoapFault $fault) {
      throw new SoapFault(1, $fault);
    }
    
    return $result->result;
  }
  
  /**
   * add a SalesOrder to Ascent API
   * 
   * global id addSO(string AccountID,
   * string ContactID,
   * String shippingInstructions,
   * Double ShippingCost,
   * Double salesTax,
   * double discount)
   * From WSDL
   * <xsd:element name="addSO">
   * <xsd:complexType>
   * <xsd:sequence>
   * <xsd:element name="AccountId" type="xsd:string" nillable="true"/>
   * <xsd:element name="contactId" type="xsd:string" nillable="true"/>
   * <xsd:element name="shippingInstructions" type="xsd:string" nillable="true"/>
   * <xsd:element name="ShippingCost" type="xsd:double" nillable="true"/>
   * <xsd:element name="salesTax" type="xsd:double" nillable="true"/>
   * <xsd:element name="discount" type="xsd:double" nillable="true"/>
   * </xsd:sequence>
   * </xsd:complexType>
   * </xsd:element>
   */
  public function addSalesOrder($accountId, $contactId, $shippingInstructions, $shippingCost=0.00, $salesTax =0.00, $discount=0.00, $order_id) {
    
    $this->_setSessionHeader($this->ascent);
    
    try {
      
      $values = array (
        'AccountId' => $accountId, 
        'contactId' => $contactId, 
        'shippingInstructions' => strip_tags($shippingInstructions), 
        'shippingCost' => $shippingCost, 
        'salesTax' => $salesTax, // @todo what is salestax based on?
        'discount' => $discount,  // @todo integrate with vouchers? 
      );
      
      $result = $this->ascent->addSO($values);
    
    }
    catch (SoapFault $fault) {
      throw new SoapFault(1, $fault);
    }
    // @todo: update any additional fields here?
    return $result->result;
  }
  
  private function setHeaders($call = NULL) {
    $this->sforce->__setSoapHeaders(NULL);
    
    $header_array = array (
      $this->sessionHeader 
    );
    
    $header = $this->callOptions;
    if ($header != NULL) {
      array_push($header_array, $header);
    }
    
    if ($call == "create" || $call == "merge" || $call == "update" || $call == "upsert") {
      $header = $this->assignmentRuleHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    if ($call == "login") {
      $header = $this->loginScopeHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    if ($call == "create" || $call == "resetPassword" || $call == "update" || $call == "upsert") {
      $header = $this->emailHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    if ($call == "create" || $call == "merge" || $call == "query" || $call == "retrieve" || $call == "update" || $call == "upsert") {
      $header = $this->mruHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    if ($call == "delete") {
      $header = $this->userTerritoryDeleteHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    if ($call == "query" || $call == "queryMore" || $call == "retrieve") {
      $header = $this->queryHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    // try to add allowFieldTruncationHeader
    $allowFieldTruncationHeaderCalls = array (
      'convertLead', 'create', 'merge', 'process', 'undelete', 'update', 'upsert' 
    );
    if (in_array($call, $allowFieldTruncationHeaderCalls)) {
      $header = $this->allowFieldTruncationHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    // try to add localeOptions
    if ($call == 'describeSObject' || $call == 'describeSObjects') {
      $header = $this->localeOptions;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    // try to add PackageVersionHeader
    $packageVersionHeaderCalls = array (
      'convertLead', 'create', 'delete', 'describeGlobal', 'describeLayout', 'describeSObject', 'describeSObjects', 'describeSoftphoneLayout', 'describeTabs', 'merge', 'process', 'query', 'retrieve', 'search', 'undelete', 'update', 'upsert' 
    );
    if (in_array($call, $packageVersionHeaderCalls)) {
      $header = $this->packageVersionHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    $this->sforce->__setSoapHeaders($header_array);
  
  }
  
  function _setSessionHeader(&$soapInterface) {
    $strHeaderComponent_Session = "<SessionHeader><sessionId>{$this->sessionId}</sessionId></SessionHeader>";
    $objVar_Session_Inside = new SoapVar($strHeaderComponent_Session, XSD_ANYXML, null, null, null);
    $objHeader_Session_Outside = new SoapHeader('http://soap.sforce.com/schemas/class/PBSI/Ascent_API', 'SessionHeader', $objVar_Session_Inside);
    $soapInterface->__setSoapHeaders(array (
      $objHeader_Session_Outside 
    ));
  }

}

